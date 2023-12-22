#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <termios.h>
#include <assert.h>

#include <event2/event.h>
#include <event2/util.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>


#include "mavlink/common/mavlink.h"

#define MAX_MTU 9000

bool verbose = false;

const char *default_master = "/dev/ttyAMA0";
const int default_baudrate = 115200;
const char *defualt_out_addr = "127.0.0.1:14600";
const char *default_in_addr =  "127.0.0.1:0";
const int RC_CHANNELS = 65; //RC_CHANNELS ( #65 ) for regular MAVLINK RC Channels read (https://mavlink.io/en/messages/common.html#RC_CHANNELS)
const int RC_CHANNELS_RAW = 35; //RC_CHANNELS_RAW ( #35 ) for ExpressLRS,Crossfire and other RC procotols (https://mavlink.io/en/messages/common.html#RC_CHANNELS_RAW)



uint8_t ch_count = 0;
uint16_t ch[14];

struct bufferevent *serial_bev;
struct sockaddr_in sin_out = {
	.sin_family = AF_INET,
};
int out_sock;

long wait_after_bash=2000; //Time to wait between bash script starts.
int ChannelPersistPeriodmMS=2000;//time needed for a RC channel value to persist to execute a commands

long aggregate=1;

static void print_usage()
{
	printf("Usage: mavfwd [OPTIONS]\n"
	       "Where:\n"
	       "  -m --master      Local MAVLink master port (%s by default)\n"
	       "  -b --baudrate    Serial port baudrate (%d by default)\n"
	       "  -o --out         Remote output port (%s by default)\n"
	       "  -i --in          Remote input port (%s by default)\n"
	       "  -c --channels    RC Channel to listen for commands (0 by default) and call channels.sh\n"
	       "  -w --wait        Delay after each command received(2000ms default)\n"
		   "  -p --persist     How long a channel value must persist to generate a command - for multiposition switches (0ms default)\n"
	       "  -a --aggregate   Aggregate packets in frames (1 no aggregation, 0 no parsing only raw data forward) (%d by default) \n"
	       "  -f --folder      Folder for file mavlink.msg (default is current folder)\n"	       	    
	       "  -t --temp        Inject SoC temperature into telemetry\n"
	       "  -v --verbose     Display each packet, default not\n"	       
	       "  --help         Display this help\n",
	       default_master, default_baudrate, defualt_out_addr,
	       default_in_addr, aggregate);
}

static speed_t speed_by_value(int baudrate)
{
	switch (baudrate) {
	case 9600:
		return B9600;
	case 19200:
		return B19200;
	case 38400:
		return B38400;
	case 57600:
		return B57600;
	case 115200:
		return B115200;
	case 230400:
		return B230400;
	case 460800:
		return B460800;
	case 500000:
		return B500000;
	case 921600:
		return B921600;
	case 1500000:
		return B1500000;
	default:
		printf("Not implemented baudrate %d\n", baudrate);
		exit(EXIT_FAILURE);
	}
}

static bool parse_host_port(const char *s, struct in_addr *out_addr,
			    in_port_t *out_port)
{
	char host_and_port[32] = { 0 };
	strncpy(host_and_port, s, sizeof(host_and_port) - 1);

	char *colon = strchr(host_and_port, ':');
	if (NULL == colon) {
		return -1;
	}

	*colon = '\0';
	const char *host = host_and_port, *port_ptr = colon + 1;

	const bool is_valid_addr = inet_aton(host, out_addr) != 0;
	if (!is_valid_addr) {
		printf("Cannot parse host `%s'.\n", host);
		return false;
	}

	int port;
	if (sscanf(port_ptr, "%d", &port) != 1) {
		printf("Cannot parse port `%s'.\n", port_ptr);
		return false;
	}
	*out_port = htons(port);

	return true;
}

static void signal_cb(evutil_socket_t fd, short event, void *arg)
{
	struct event_base *base = arg;
	(void)event;

	printf("%s signal received\n", strsignal(fd));
	event_base_loopbreak(base);
}

static void dump_mavlink_packet(unsigned char *data, const char *direction)
{
  uint8_t seq;
  uint8_t sys_id;
  uint8_t comp_id;
  uint32_t msg_id;

  if(data[0] == 0xFE) { //mavlink 1
      seq = data[2];
      sys_id = data[3];
      comp_id = data[4];
      msg_id = data[5];
  } else { //mavlink 2
      seq = data[4];
      sys_id = data[5];
      comp_id = data[6];
      msg_id = data[7];
  }

	if (verbose) printf("%s %#02x sender %d/%d\t%d\t%d\n", direction, data[0], sys_id, comp_id, seq, msg_id);
  
  uint16_t val;
  
	if((msg_id == RC_CHANNELS || msg_id == RC_CHANNELS_RAW ) && ch_count > 0) {
      uint8_t offset = 18; //15 = 1ch;
      for(uint8_t i=0; i < ch_count; i++) {
          val = data[offset] | (data[offset+1] << 8);
          if(ch[i] != val) {
              ch[i] = val;
              char buff[44];
              sprintf(buff, "channels.sh %d %d &", i+5, val);
              system(buff);
              if (verbose) printf("called channels.sh %d %d\n", i+5, val);
           }
      offset = offset + 2;
	    } //for
	} //msg_id
}

/* https://discuss.ardupilot.org/uploads/short-url/vS0JJd3BQfN9uF4DkY7bAeb6Svd.pdf
 * 0. Message header, always 0xFE
 * 1. Message length
 * 2. Sequence number -- rolls around from 255 to 0 (0x4e, previous was 0x4d)
 * 3. System ID - what system is sending this message
 * 4. Component ID- what component of the system is sending the message
 * 5. Message ID (e.g. 0 = heartbeat and many more! Don’t be shy, you can add too..)
 */
static bool get_mavlink_packet(unsigned char *in_buffer, int buf_len,
			       int *packet_len)
{
	if (buf_len < 6 /* header */) {
		return false;
	}
	assert(in_buffer[0] == 0xFE || in_buffer[0] == 0xFD); //mavlink 1 or 2

	uint8_t msg_len = in_buffer[1];
	if (in_buffer[0] == 0xFE)
    *packet_len = 6 /* header */ + msg_len + 2 /* crc */; //mavlink 1
  else
    *packet_len = 10 /* header */ + msg_len + 2 /* crc */; //mavlink 2
	if (buf_len < *packet_len)
		return false;

	dump_mavlink_packet(in_buffer, ">>");

	return true;
}

// Returns num bytes before first occurrence of 0xFE or full data length
static size_t until_first_fe(unsigned char *data, size_t len)
{
	for (size_t i = 1; i < len; i++) {
		if (data[i] == 0xFE || data[i] == 0xFD) {
			return i;
		}
	}
	return len;
}

#define MAX_BUFFER_SIZE 50 //Limited by mavlink

static char MavLinkMsgFile[128]= "mavlink.msg";  

bool Check4MavlinkMsg(char* buffer) {
    //const char *filename = MavLinkMsgFile;//"mavlink.msg";
    
    FILE *file = fopen(MavLinkMsgFile, "rb");
    if (file == NULL)// No file, no problem
        return false;
        
    size_t bytesRead = fread(buffer, 1, MAX_BUFFER_SIZE, file);
    fclose(file);

    if (bytesRead > 0) {        
		if (verbose)
        	printf("Mavlink msg from file:%s\n", buffer);
        if (remove(MavLinkMsgFile) != 0)             
        	printf("Error deleting file");        
		return true;
    } else 
        printf("Mavlink empty file ?!\n");    

	return false;
}
static uint8_t system_id=1;

static bool SendInfoToGround(){
	char msg_buf[MAX_BUFFER_SIZE];
	if (!Check4MavlinkMsg(&msg_buf[0]))
		return false;

	//Huston, we have a message for you.
    mavlink_message_t message;
 	
    mavlink_msg_statustext_pack_chan(
        system_id,
        MAV_COMP_ID_SYSTEM_CONTROL,
        MAVLINK_COMM_1,
        &message,
		4,  	// 4 - Warning, 5 - Error
		msg_buf,
		0,0);

    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    const int len = mavlink_msg_to_send_buffer(buffer, &message);
		
	sendto(out_sock, buffer, len, 0, (struct sockaddr *)&sin_out, sizeof(sin_out));

	return true;    
}

unsigned long long get_current_time_ms() {
    time_t current_time = time(NULL);
    return (unsigned long long)(current_time * 1000);
}

static unsigned long LastTempSent;
static float last_board_temp;

static int SendTempToGround(unsigned char* mavbuf){

	if ( abs(get_current_time_ms()-LastTempSent) < 1000)//Once a second
		return 0;

	LastTempSent = abs(get_current_time_ms());

	char msg_buf[MAX_BUFFER_SIZE];
    mavlink_message_t message;
 	
    mavlink_msg_raw_imu_pack_chan(
        system_id,
        MAV_COMP_ID_SYSTEM_CONTROL,
        MAVLINK_COMM_1,
        &message,0,0,0,0,0,0,0,0,0,0,0,
		last_board_temp*100	
		);

    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    const int len = mavlink_msg_to_send_buffer(mavbuf, &message);
	//printf("temp %f sent with %d bytes",last_board_temp*100,len);		

	return len;    
}

bool version_shown=false;
static void ShowVersionOnce(mavlink_message_t* msg, uint8_t header){
	if (version_shown)
		return;
	version_shown=true;
/*
The major version can be determined from the packet start marker byte:
MAVLink 1: 0xFE
MAVLink 2: 0xFD
*/
	if (header==0xFE)
		printf("Detected MAVLink ver: 1.0  (%d)\n",msg->magic);
	if (header==0xFD)
		printf("Detected MAVLink version: 2.0  (%d)\n",msg->magic);

	printf("System_id = %d \n",system_id);		

}
bool fc_shown=false;
void handle_heartbeat(const mavlink_message_t* message)
{
	if (fc_shown)
		return;
    fc_shown=true;

    mavlink_heartbeat_t heartbeat;
    mavlink_msg_heartbeat_decode(message, &heartbeat);

    printf("Flight Controller Type :");
    switch (heartbeat.autopilot) {
        case MAV_AUTOPILOT_GENERIC:
            printf("Generic/INAV");
            break;
        case MAV_AUTOPILOT_ARDUPILOTMEGA:
            printf("ArduPilot");
            break;
        case MAV_AUTOPILOT_PX4:
            printf("PX4");
            break;
        default:
            printf("other");
            break;
    }
	printf("\n");    
}


void handle_statustext(const mavlink_message_t* message)
{	
    mavlink_statustext_t statustext;
    mavlink_msg_statustext_decode(message, &statustext);		
    printf("FC message:%s", statustext.text);

}

unsigned long long get_current_time_ms2() {
    clock_t current_clock_ticks = clock();	
    return (unsigned long long)(current_clock_ticks * 1000 / CLOCKS_PER_SEC);
	
}



uint16_t channels[18];

//how long a RC value should stay at one level to issue a command

unsigned static long LastStart=0;//get_current_time_ms();
unsigned static long LastValue=0;
uint16_t NewValue;
unsigned static long NewValueStart=0;
unsigned int ChannelCmds=0;

void ProcessChannels(){
	//ch_count , not zero based, 1 is first
	uint16_t val=0;
	if (ch_count<1 || ch_count>16)
		return;

	if ( abs(get_current_time_ms()-LastStart) < wait_after_bash)
		return;

	val=channels[ch_count-1];
	
	if (abs(val-NewValue)>32 && ChannelPersistPeriodmMS>0){
		//We have a new value, let us wait for it to persist
		NewValue=val;
		NewValueStart=get_current_time_ms();
		return;
	}else
		if (abs(get_current_time_ms()-NewValueStart)<ChannelPersistPeriodmMS)		
			return;//New value should remain "stable" for a second before being approved					
		else{}//New value persisted for more THAN ChannelPersistPeriodmMS
					

	if (abs(val-LastValue)<32)//the change is too small
		return;
	
	NewValue=val;
	LastValue=val;
	char buff[44];
    sprintf(buff, "channels.sh %d %d &", ch_count, val);
	LastStart=get_current_time_ms();

	if (ChannelCmds>0){//intentionally skip the first command, since when stating mavfwd it will always receive some channel value and execute the script
    	system(buff);
		printf("Called channels.sh %d %d\n", ch_count, val);
	}
	ChannelCmds++;
	
}

void showchannels(int count){
	if (verbose){
		printf("Channels :"); 
		for (int i =0; i <count;i++)
			printf("| %02d", channels[i]);
		printf("\n");
	}
}

void handle_msg_id_rc_channels_raw(const mavlink_message_t* message){
	mavlink_rc_channels_raw_t rc_channels;
	mavlink_msg_rc_channels_raw_decode(message, &rc_channels);	
	memcpy(&channels[0], &rc_channels.chan1_raw, 8 * sizeof(uint16_t));	
	showchannels(8);
		
	ProcessChannels();
}


void handle_msg_id_rc_channels_override(const mavlink_message_t* message){
	mavlink_rc_channels_override_t rc_channels;
	mavlink_msg_rc_channels_override_decode(message, &rc_channels);	
	memcpy(&channels[0], &rc_channels.chan1_raw, 18 * sizeof(uint16_t));	
	showchannels(18);
	ProcessChannels();
}

void handle_msg_id_rc_channels(const mavlink_message_t* message){
	mavlink_rc_channels_t rc_channels;
	mavlink_msg_rc_channels_decode(message, &rc_channels);	
	memcpy(&channels[0], &rc_channels.chan1_raw, 18 * sizeof(uint16_t));	
	showchannels(18);
	ProcessChannels();
}

unsigned char mavbuf[2048];
unsigned int mavbuff_offset=0;
unsigned int mavpckts_count=0;
static void process_mavlink(uint8_t* buffer, int count, void *arg){

    mavlink_message_t message;
    mavlink_status_t status;
    for (int i = 0; i < count; ++i) {
		
		if (mavbuff_offset>2000){
			printf("Mavlink buffer overflowed! Packed lost!\n");
			mavbuff_offset=0;
		}
		mavbuf[mavbuff_offset]=buffer[i];
		mavbuff_offset++;
        if (mavlink_parse_char(MAVLINK_COMM_0, buffer[i], &message, &status) == 1) {
			system_id=message.sysid;
			ShowVersionOnce(&message, (uint8_t) buffer[0]);
			if (verbose)
        	    printf("Mavlink msg %d no: %d\n",message.msgid, message.seq);
            switch (message.msgid) {
            	case MAVLINK_MSG_ID_RC_CHANNELS_RAW://35 Used by INAV
	                handle_msg_id_rc_channels_raw(&message);
	                break;
	            case MAVLINK_MSG_ID_RC_CHANNELS_OVERRIDE://70
    	            handle_msg_id_rc_channels_override(&message);
                	break;

				case MAVLINK_MSG_ID_RC_CHANNELS://65 used by ArduPilot
	                handle_msg_id_rc_channels(&message);
                	break;

             	case MAVLINK_MSG_ID_HEARTBEAT://Msg info from the FC
	                handle_heartbeat(&message);
                	break;
				case MAVLINK_MSG_ID_STATUSTEXT://Msg info from the FC					
	                //handle_statustext(&message);
                	break;					
            }
			mavpckts_count++;			
			bool mustflush=false;
			if (aggregate>0){//We will send whole packets only				
				if (
					((aggregate>=1 && aggregate<50 ) && mavpckts_count>=aggregate ) ||  //if packets more than treshold
					((aggregate>50 && aggregate<2000 ) && mavbuff_offset>=aggregate ) || //if buffer  more than treshold
					((mavpckts_count>=3) && message.msgid==MAVLINK_MSG_ID_ATTITUDE )	//MAVLINK_MSG_ID_ATTITUDE will always cause the buffer flushed
				){
					//flush and send all data
					if (sendto(out_sock, mavbuf, mavbuff_offset, 0, (struct sockaddr *)&sin_out, sizeof(sin_out)) == -1) {
						perror("sendto()");
						event_base_loopbreak(arg);
					}
					if (verbose)
						printf("%d Pckts / %d bytes sent\n",mavpckts_count,mavbuff_offset);
					mavbuff_offset=0;
					mavpckts_count=0;
					SendInfoToGround();

					if (last_board_temp>-100)
						mavbuff_offset=SendTempToGround(mavbuf);
					if (mavbuff_offset>0){
						mavpckts_count++;
					}

				}
			}
        }//if mavlink_parse_char
    }
}

static long ttl_packets=0;
static long ttl_bytes=0;

static void serial_read_cb(struct bufferevent *bev, void *arg)
{
	struct evbuffer *input = bufferevent_get_input(bev);
	int packet_len, in_len;
	struct event_base *base = arg;	

	while ((in_len = evbuffer_get_length(input))) {
		unsigned char *data = evbuffer_pullup(input, in_len);
		if (data == NULL) {
			return;
		}
		packet_len = in_len;

		//First forward all serial input to UDP.
		ttl_packets++;
		ttl_bytes+=packet_len;

		if (!version_shown && ttl_packets%10==3)//If garbage only, give some feedback do diagnose
			printf("Packets:%d  Bytes:%d\n",ttl_packets,ttl_bytes);

		if (aggregate==0){
			if (sendto(out_sock, data, packet_len, 0,
			   (struct sockaddr *)&sin_out,
			   sizeof(sin_out)) == -1) {
					perror("sendto()");
					event_base_loopbreak(base);
			}
		}

		//Let's try to parse the stream	
		if (aggregate>0 || ch_count>0)//if no RC channel control needed, only forward the data
			process_mavlink(data,packet_len, arg);//Let's try to parse the stream		

		evbuffer_drain(input, packet_len);
	}
}



// Signal handler function
void sendtestmsg(int signum) {
	printf("Sending test mavlink msg.\n");
	char buff[200];
	sprintf(buff, "echo Hello_From_OpenIPC > %s", MavLinkMsgFile);
	system(buff);
	SendInfoToGround();     
}


static void serial_event_cb(struct bufferevent *bev, short events, void *arg)
{
	(void)bev;
	struct event_base *base = arg;

	if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR | BEV_EVENT_TIMEOUT)) {
		printf("Serial connection closed\n");
		event_base_loopbreak(base);
	}
}

static void in_read(evutil_socket_t sock, short event, void *arg)
{
	(void)event;
	unsigned char buf[MAX_MTU];
	struct event_base *base = arg;
	ssize_t nread;

	nread = recvfrom(sock, &buf, sizeof(buf) - 1, 0, NULL, NULL);
	if (nread == -1) {
		perror("recvfrom()");
		event_base_loopbreak(base);
	}

	assert(nread > 6);

	dump_mavlink_packet(buf, "<<");

	bufferevent_write(serial_bev, buf, nread);
}


static void* setup_temp_mem(off_t base, size_t size)
{
	int mem_fd;

	mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (mem_fd < 0) {
		fprintf(stderr, "can't open /dev/mem\n");
		return NULL;
	}

	char *mapped_area =
		mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, base);
	if (mapped_area == MAP_FAILED) {
		fprintf(stderr, "read_mem_reg mmap error: %s (%d)\n",
				strerror(errno), errno);
		return NULL;
	}

	uint32_t MISC_CTRL45 = 0;

	// Set the T-Sensor cyclic capture mode by configuring MISC_CTRL45 bit[30]
	MISC_CTRL45 |= 1 << 30;

	// Set the capture period by configuring MISC_CTRL45 bit[27:20]
	// The formula for calculating the cyclic capture periodis as follows:
	//	T = N x 2 (ms)
	//	N is the value of MISC_CTRL45 bit[27:20]
	MISC_CTRL45 |= 50 << 20;

	// Enable the T-Sensor by configuring MISC_CTRL45 bit[31] and start to collect the temperature
	MISC_CTRL45 |= 1 << 31;

	*(volatile uint32_t *)(mapped_area + 0xB4) = MISC_CTRL45;

	return mapped_area;
}

static void temp_read(evutil_socket_t sock, short event, void *arg)
{
	(void)sock;
	(void)event;
	char *mapped_area = arg;

	uint32_t val = *(volatile uint32_t *)(mapped_area + 0xBC);
	float tempo = val & ((1 << 16) - 1);
        tempo = ((tempo - 117) / 798) * 165 - 40;

	if (last_board_temp == -100)//only once
		printf("Temp read %f C\n", tempo);
	last_board_temp=tempo;
}

static int handle_data(const char *port_name, int baudrate,
		       const char *out_addr, const char *in_addr, bool temp)
{
	struct event_base *base = NULL;
	struct event *sig_int = NULL, *in_ev = NULL, *temp_tmr = NULL;
	int ret = EXIT_SUCCESS;

	int serial_fd = open(port_name, O_RDWR | O_NOCTTY);
	if (serial_fd < 0) {
		printf("Error while openning port %s: %s\n", port_name,
		       strerror(errno));
		return EXIT_FAILURE;
	};
	evutil_make_socket_nonblocking(serial_fd);

	struct termios options;
	tcgetattr(serial_fd, &options);
	cfsetspeed(&options, speed_by_value(baudrate));

	options.c_cflag &= ~CSIZE; // Mask the character size bits
	options.c_cflag |= CS8; // 8 bit data
	options.c_cflag &= ~PARENB; // set parity to no
	options.c_cflag &= ~PARODD; // set parity to no
	options.c_cflag &= ~CSTOPB; // set one stop bit

	options.c_cflag |= (CLOCAL | CREAD);

	options.c_oflag &= ~OPOST;

	options.c_lflag &= 0;
	options.c_iflag &= 0; // disable software flow controll
	options.c_oflag &= 0;

	cfmakeraw(&options);
	tcsetattr(serial_fd, TCSANOW, &options);

	out_sock = socket(AF_INET, SOCK_DGRAM, 0);

	int in_sock = 0;


	printf("Listening on %s...\n", port_name);

	struct sockaddr_in sin_in = {
		.sin_family = AF_INET,
	};
	if (!parse_host_port(in_addr, (struct in_addr *)&sin_in.sin_addr.s_addr,
			     &sin_in.sin_port))
		goto err;
	if (!parse_host_port(out_addr,
			     (struct in_addr *)&sin_out.sin_addr.s_addr,
			     &sin_out.sin_port))
		goto err;

	if (sin_in.sin_port>0)
		in_sock=socket(AF_INET, SOCK_DGRAM, 0);

	if (in_sock>0 && bind(in_sock, (struct sockaddr *)&sin_in, sizeof(sin_in))) {// we may not need this
		perror("bind()");
		exit(EXIT_FAILURE);
	}
	if(in_sock>0)
		printf("Listening on %s...\n", in_addr);

	base = event_base_new();

	sig_int = evsignal_new(base, SIGINT, signal_cb, base);
	event_add(sig_int, NULL);
	// it's recommended by libevent authors to ignore SIGPIPE
	signal(SIGPIPE, SIG_IGN);

	//Test inject a simple packet to test malvink communication Camera to Ground
	signal(SIGUSR1, sendtestmsg);

	serial_bev = bufferevent_socket_new(base, serial_fd, 0);
	bufferevent_setcb(serial_bev, serial_read_cb, NULL, serial_event_cb,
			  base);
	bufferevent_enable(serial_bev, EV_READ);

	if (in_sock>0){
		in_ev = event_new(base, in_sock, EV_READ | EV_PERSIST, in_read, NULL);
		event_add(in_ev, NULL);
	}
	if (temp) {
		void* mem = setup_temp_mem(0x12028000, 0xFFFF);
		temp_tmr = event_new(base, -1, EV_PERSIST, temp_read, mem);
		evtimer_add(temp_tmr, &(struct timeval){.tv_sec = 1});
	}

	event_base_dispatch(base);

err:
	if (temp_tmr) {
		event_del(temp_tmr);
		event_free(temp_tmr);
	}
	if (out_sock>0)
		close(out_sock);

	if (in_sock>0)
		close(in_sock);

	if (serial_fd >= 0)
		close(serial_fd);

	if (serial_bev)
		bufferevent_free(serial_bev);

	if (in_ev) {
		event_del(in_ev);
		event_free(in_ev);
	}

	if (sig_int)
		event_free(sig_int);

	if (base)
		event_base_free(base);

	libevent_global_shutdown();

	return ret;
}

//COMPILE : gcc -o program mavfwd.c -levent -levent_core

int main(int argc, char **argv)
{
	const struct option long_options[] = {
		{ "master", required_argument, NULL, 'm' },
		{ "baudrate", required_argument, NULL, 'b' },
		{ "out", required_argument, NULL, 'o' },
		{ "aggregate", required_argument, NULL, 'a' },
		{ "in", required_argument, NULL, 'i' },
		{ "channels", required_argument, NULL, 'c' },
		{ "wait_time", required_argument, NULL, 'w' },				
		{ "folder", required_argument, NULL, 'f' },				
		{ "persist", required_argument, NULL, 'p' },
		{ "verbose", no_argument, NULL, 'v' },		
		{ "temp", no_argument, NULL, 't' },						
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	const char *port_name = default_master;
	int baudrate = default_baudrate;
	const char *out_addr = defualt_out_addr;
	const char *in_addr = default_in_addr;
	int temp = false;
	last_board_temp=-100;

	int opt;
	int long_index = 0;
	while ((opt = getopt_long_only(argc, argv, "", long_options, &long_index)) != -1) {
		switch (opt) {
		case 'm':
			port_name = optarg;
			break;
		case 'b':
			baudrate = atoi(optarg);
			break;
		case 'o':
			out_addr = optarg;
			break;
		case 'i':
			in_addr = optarg;
			break;
		case 'a':	
			aggregate = atoi(optarg);
			if (aggregate>2000)
				aggregate=2000;

			if(aggregate == 0) 
				printf("No parsing, raw UART to UDP only\n");
			else if (aggregate<50)
				printf("Aggregate mavlink pckts in packs of %d \n", aggregate);
			else if (aggregate>50)
				printf("Aggregate mavlink pckts till buffer reaches %d bytes \n", aggregate);
						
		break;
		case 'c':
			ch_count = atoi(optarg);
			if(ch_count == 0) 
				printf("rc_channels  monitoring disabled\n");
			else 
				printf("Monitoring RC channel %d \n", ch_count);
			
			LastStart=get_current_time_ms();
			break;

		case 'f':
			 if (optarg != NULL) {        		
        		
        		snprintf(MavLinkMsgFile, sizeof(MavLinkMsgFile), "%smavlink.msg", optarg);
			 }

			break;
		case 'w':
			wait_after_bash = atoi(optarg);			
			LastStart=get_current_time_ms();
			break;

		case 'p':
			ChannelPersistPeriodmMS = atoi(optarg);			
			LastStart=get_current_time_ms();
			break;

		case 't':		

			temp = true;
			break;

		case 'v':
			verbose = true;
			printf("Verbose mode!");
			break;			

		case 'h':
		default:
			print_usage();
			return EXIT_SUCCESS;
		}
	}	

	return handle_data(port_name, baudrate, out_addr, in_addr, temp);
}

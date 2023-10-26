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

const char *default_master = "/dev/ttyAMA0";
const int default_baudrate = 115200;
const char *defualt_out_addr = "127.0.0.1:14600";
const char *default_in_addr = "127.0.0.1:14601";

struct bufferevent *serial_bev;
struct sockaddr_in sin_out = {
	.sin_family = AF_INET,
};
int out_sock;

uint8_t ch_count = 0;
bool verbose = false;
long wait_after_bash=1000; //Time to wait between bash script starts.

static void print_usage()
{
	printf("Usage: mavfwd [OPTIONS]\n"
	       "Where:\n"
	       "  --master        Local MAVLink master port (%s by default)\n"
	       "  --baudrate      Serial port baudrate (%d by default)\n"
	       "  --out           Remote output port (%s by default)\n"
	       "  --in            Remote input port (%s by default)\n"
		   "  --c             RC Channel to listen for commands (0 by default)\n"
		   "  --w             Delay after each command received\n"
	       "  --in            Remote input port (%s by default)\n"
	       "  --help          Display this help\n",
	       default_master, default_baudrate, defualt_out_addr,
	       default_in_addr);
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
	uint8_t seq = data[2];
	uint8_t sys_id = data[3];
	uint8_t comp_id = data[4];
	uint8_t msg_id = data[5];

	printf("%s sender %d/%d\t%d\t%d\n", direction, sys_id, comp_id, seq,
	       msg_id);
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
	assert(in_buffer[0] == 0xFE);

	uint8_t msg_len = in_buffer[1];
	*packet_len = 6 /* header */ + msg_len + 2 /* crc */;
	if (buf_len < *packet_len)
		return false;

	dump_mavlink_packet(in_buffer, ">>");

	return true;
}

// Returns num bytes before first occurrence of 0xFE or full data length
static size_t until_first_fe(unsigned char *data, size_t len)
{
	for (size_t i = 1; i < len; i++) {
		if (data[i] == 0xFE) {
			return i;
		}
	}
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
		printf("MAVLink version: 1.0\n");
	if (header==0xFD)
		printf("MAVLink version: 2.0\n");

	  //if (msg->magic == MAVLINK_STX)             
    printf("Magic : %d  \n",msg->magic);
   
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
            printf("generic");
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

unsigned long long get_current_time_ms2() {
    clock_t current_clock_ticks = clock();	
    return (unsigned long long)(current_clock_ticks * 1000 / CLOCKS_PER_SEC);
	
}
unsigned long long get_current_time_ms() {
    time_t current_time = time(NULL);
    return (unsigned long long)(current_time * 1000);
}


uint16_t channels[18];

unsigned static long LastStart=0;//get_current_time_ms();
unsigned static long LastValue=0;

void ProcessChannels(){
	//ch_count , not zero based, 1 is first
	uint16_t val=0;
	if (ch_count<1 || ch_count>16)
		return;

	if ( abs(get_current_time_ms()-LastStart) < wait_after_bash)
		return;

	val=channels[ch_count-1];

	if (val==LastValue)
		return;

	if (abs(val-LastValue)<32)//the change is too small
		return;

	LastValue=val;
	char buff[44];
    sprintf(buff, "channels.sh %d %d &", ch_count, val);
	LastStart=get_current_time_ms();
    system(buff);
	
   printf("Called channels.sh %d %d\n", ch_count, val);

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


static void process_mavlink(uint8_t* buffer, int count){

    mavlink_message_t message;
    mavlink_status_t status;
    for (int i = 0; i < count; ++i) {
        if (mavlink_parse_char(MAVLINK_COMM_0, buffer[i], &message, &status) == 1) {
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
            }
        }
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

		if (sendto(out_sock, data, packet_len, 0,
			   (struct sockaddr *)&sin_out,
			   sizeof(sin_out)) == -1) {
			perror("sendto()");
			event_base_loopbreak(base);
		}

		//Let's try to parse the stream	
		if (ch_count>0)//if no RC channel control needed, only forward the data
			process_mavlink(data,packet_len);//Let's try to parse the stream	
		evbuffer_drain(input, packet_len);
	}
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

static int handle_data(const char *port_name, int baudrate,
		       const char *out_addr, const char *in_addr)
{
	struct event_base *base = NULL;
	struct event *sig_int = NULL, *in_ev = NULL;
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
	int in_sock = socket(AF_INET, SOCK_DGRAM, 0);
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

	if (bind(in_sock, (struct sockaddr *)&sin_in, sizeof(sin_in))) {
		perror("bind()");
		exit(EXIT_FAILURE);
	}
	printf("Listening on %s...\n", in_addr);

	base = event_base_new();

	sig_int = evsignal_new(base, SIGINT, signal_cb, base);
	event_add(sig_int, NULL);
	// it's recommended by libevent authors to ignore SIGPIPE
	signal(SIGPIPE, SIG_IGN);

	serial_bev = bufferevent_socket_new(base, serial_fd, 0);
	bufferevent_setcb(serial_bev, serial_read_cb, NULL, serial_event_cb,
			  base);
	bufferevent_enable(serial_bev, EV_READ);

	in_ev = event_new(base, in_sock, EV_READ | EV_PERSIST, in_read, NULL);
	event_add(in_ev, NULL);

	event_base_dispatch(base);

err:
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
		{ "in", required_argument, NULL, 'i' },
		{ "channel", required_argument, NULL, 'c' },
		{ "wait_time", required_argument, NULL, 'w' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	const char *port_name = default_master;
	int baudrate = default_baudrate;
	const char *out_addr = defualt_out_addr;
	const char *in_addr = default_in_addr;

	int opt;
	int long_index = 0;
	while ((opt = getopt_long_only(argc, argv, "", long_options,
				       &long_index)) != -1) {
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
		case 'c':
			ch_count = atoi(optarg);
			if(ch_count == 0) 
				printf("rc_channels  monitoring disabled\n");
			else 
				printf("Monitoring channel %d \n", ch_count);

			LastStart=get_current_time_ms();
			break;
		case 'w':
			wait_after_bash = atoi(optarg);			
			LastStart=get_current_time_ms();
			break;
		case 'v':
			verbose = true;
			break;			
		case 'h':
		default:
			print_usage();
			return EXIT_SUCCESS;
		}
	}

	return handle_data(port_name, baudrate, out_addr, in_addr);
}

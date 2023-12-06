 # MAVFWD
Added mavlink library parsing.
mavfwd is a utility for broadcasting a mavlink telemetry stream between wifibroadcast and uart devices, for organizing one- or two-way telemetry between UAV and ground station. mavfwd is included in the OpenIPC FPV firmware as a lighter alternative to mavlink routerd and is usually used on the air part (camera).
Also, mavfwd can monitor the mavlink RC _CHANNELS package, and call the script `channels.sh` (located at /usr/bin or /usr/sbin) passing the channel number and its value to it when changing as $1 and $2 parameters. An example of channels.sh is present.
This allows for controlling the camera via the Remote Control Transmitter
```
Usage: mavfwd [OPTIONS]\n"
	       Where:\n"
            --master        Local MAVLink master port (%s by default)
            --baudrate      Serial port baudrate (%d by default)
            --out           Remote output port (%s by default)
            --in            Remote input port (%s by default)
            --channels       RC Channel to listen for commands (0 by default)
            --w             Delay after each command received(2000ms defaulr)
            --in            Remote input port (%s by default)
            --aggregate     Aggregate Packets - [1-20] Pckt count before flush.  [50-1400] buffer size in bytes before flush)
            --tempfolder    folder where a temp file will be read, send in a mavlink message and displayed in the HUD on the ground
            --help          Display this help
```
Examples :

```mavfwd --master /dev/ttyAMA0 --baudrate 115200 --out 192.168.1.20:14550 --c 7 --w 3000 -a 10```

Will read on the first UART with baudrade 115200 and will listen for values in RC channel 7 that come from the Remote Control via Flight Controller.
Every time the value is changed with more than 5% the bash script channels.sh {Channel} {Value} will be started with params.
The will check the Value(usually between 1000 and 2000) and will do the tasks need - reconfigure encoder, switch IR mode, etc.
To protect the system from overloading, the scritp will not be started again for 3000ms.
Packets will be aggregated in chunks of 10 into one UDP frame. A MAVLINK_MSG_ID_ATTITUDE from the FC will flush the buffer. 
This way the OSD will be updated with the same rate and no lag. 
 -a 15 : will flush the cached messages into one UDP packet after count of message reaches 15
 -a 1024 : will flush the cached messages into one UDP packet total length of all message reaches 1024 bytes
in both cases the buffer will be flushed if there are at least 3 packets and a MAVLINK_MSG_ID_ATTITUDE is received.
Option to send text from the cam. A the file mavlink.msg i {tempfolder} is monitored and when found, all data from it are send as plain text message via   MAVLINK_MSG_ID_STATUSTEXT 253 to the ground station and the file is deleted.
Sample:
echo "Huston, this is IPC" >/tmp/mavlink.msg



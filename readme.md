 # MAVFWD
Added mavlink library parsing.
mavfwd is a utility for broadcasting a mavlink telemetry stream between wifibroadcast and uart devices, for organizing one- or two-way telemetry between UAV and ground station. mavfwd is included in the OpenIPC FPV firmware as a lighter alternative to mavlink routerd and is usually used on the air part (camera).
Also, mavfwd can monitor the mavlink RC _CHANNELS package, and call the script `channels.sh` (located at /usr/bin or /usr/sbin) passing the channel number and its value to it when changing as $1 and $2 parameters. An example of channels.sh is present.
```
Usage: mavfwd [OPTIONS]\n"
	       Where:\n"
	        --master        Local MAVLink master port (%s by default)
	        --baudrate      Serial port baudrate (%d by default)
	        --out           Remote output port (%s by default)
	        --in            Remote input port (%s by default)
		     --c             RC Channel to listen for commands (0 by default)
		     --w             Delay after each command received(2000ms defaulr)
	        --in            Remote input port (%s by default)
	        --help          Display this help
```
Example
mavfwd --master /dev/ttyAMA0 --baudrate 115200 --out 192.168.1.20:14550 --c 7 --w 3000

Will read on the first UART with baudrade 115200 and will listen for values in channel 7.
Every time the value is changed with more than 5% the bash script channels.sh {Channel} {Value} will be started with params.
The will check the Value(usually between 1000 and 2000) and will do the tasks need - reconfigure encoder, switch IR mode, etc.
To protect the system from overloading, the scritp will not be started again for 3000ms.
## MAVFWD

mavfwd is a utility for broadcasting a mavlink telemetry stream between wifibroadcast and uart devices, for organizing one- or two-way telemetry between UAV and ground station. mavfwd is included in the OpenIPC FPV firmware as a lighter alternative to mavlink routerd and is usually used on the air part (camera).
Also, mavfwd can monitor the mavlink RC _CHANNELS package starting from the 5th and on the specified channel, and call the script `channels.sh` (located at /usr/bin or /usr/sbin) passing the channel number and its value to it when changing as $1 and $2 parameters. An example of channels.sh is present.
```
Usage: mavfwd [OPTIONS]
Where:
   --master Local MAVLink master port (/dev/ttyAMA0 by default)
   --baudrate Serial port baudrate (115200 by default)
   --out Remote output port (127.0.0.1:14600 by default)
   --in Remote input port (127.0.0.1:14601 by default)
   --channels RC override channels to parse after first 4 and call /root/channels.sh $ch $val, default 0
   --verbose display each packet, default not
   --help Display this help
```

A very simple UDP forwarder in pure C.

Sample usage:
udpfwd --out 127.0.0.1:5600 --out2 127.0.0.1:5601 --in 127.0.0.1:5666
Will read UDP packets from port 5666 and send them to two separate endpoints 127.0.0.1:5600 and 127.0.0.1:5601.
Each packet is resend as received, no buffering or packet aggregation applied.




echo $1 $2 >>/tmp/channels.log

#2000, 1725, 1575, 1425, 1275, 1000
# 6 position switch as defined by ELRS 
# Sample for ssc338q with IMX415

    if [ $2 -lt 1050 ]; then
		yaml-cli -s .image.contrast 70
		yaml-cli -s .image.hue 50
		yaml-cli -s .image.saturation 50
		yaml-cli -s .image.luminance 50
		
		yaml-cli -s .video0.bitrate 12000		
		yaml-cli -s .video0.size 1920x1080
		yaml-cli -s .video0.gopSize 1.5
		yaml-cli -s .video0.fps 60
		
		yaml-cli -s .isp.exposure: 9				
		echo "1920x1080/60fps/9ms" >/tmp/mavlink.msg
		
		killall -1 majestic
      
    elif [ $2 -gt 1250 ] && [ $2 -lt 1300 ]; then
		yaml-cli -s .image.contrast 70
		yaml-cli -s .image.hue 50
		yaml-cli -s .image.saturation 50
		yaml-cli -s .image.luminance 50
		
		yaml-cli -s .video0.bitrate 15000		
		yaml-cli -s .video0.size 1920x1080
		yaml-cli -s .video0.gopSize 1.5
		yaml-cli -s .video0.fps 60		
		yaml-cli -s .isp.exposure: 17		
		echo "1920x1080/60fps/17ms" >/tmp/mavlink.msg
		
		killall -1 majestic
		
	elif [ $2 -gt 1400 ] && [ $2 -lt 1450 ]; then
	
		yaml-cli -s .image.contrast 70
		yaml-cli -s .image.hue 50
		yaml-cli -s .image.saturation 50
		yaml-cli -s .image.luminance 50
		
		yaml-cli -s .video0.bitrate 15000		
		yaml-cli -s .video0.size 1920x1080
		yaml-cli -s .video0.gopSize 1.5
		yaml-cli -s .video0.fps 60		
		yaml-cli -s .isp.exposure: 27				
		echo "1920x1080/60fps/27ms" >/tmp/mavlink.msg
		
		killall -1 majestic		
 
    elif [ $2 -gt 1550 ] && [ $2 -lt 1600 ]; then
	
		yaml-cli -s .image.contrast 90
		yaml-cli -s .image.hue 60
		yaml-cli -s .image.saturation 60
		yaml-cli -s .image.luminance 50
		
		yaml-cli -s .video0.bitrate 15000		
		yaml-cli -s .video0.size 1920x1080
		yaml-cli -s .video0.gopSize 1.5
		yaml-cli -s .video0.fps 60		
		yaml-cli -s .isp.exposure: 9		
		echo "1920x1080/60fps/9ms Sat:60%" >/tmp/mavlink.msg
		
		killall -1 majestic
		
    elif [ $2 -gt 1700 ] && [ $2 -lt 1750 ]; then
				
		yaml-cli -s .image.contrast 60
		yaml-cli -s .image.hue 50
		yaml-cli -s .image.saturation 50
		yaml-cli -s .image.luminance 50
		
		yaml-cli -s .video0.bitrate 21000		
		yaml-cli -s .video0.size 2944x1656
		yaml-cli -s .video0.gopSize 1
		yaml-cli -s .video0.fps 30
		yaml-cli -s .isp.exposure: 39
		
		echo "2944x1656/30fps/39ms" >/tmp/mavlink.msg		
				
		killall -1 majestic
		
	elif [ $2 -gt 1950 ]; then
		yaml-cli -s .image.contrast 60
		yaml-cli -s .image.hue 50
		yaml-cli -s .image.saturation 50
		yaml-cli -s .image.luminance 50
		
		yaml-cli -s .video0.bitrate 21000		
		yaml-cli -s .video0.size 3840x2160
		yaml-cli -s .video0.gopSize 1
		yaml-cli -s .video0.fps 20
		yaml-cli -s .isp.exposure: 32		
		echo "3840x2160/20fps/32ms" >/tmp/mavlink.msg
		
		killall -1 majestic
		
    else
		echo "unknown value" $2
    fi
#fi


exit 1

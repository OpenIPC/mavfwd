
echo $1 $2 >>/tmp/channels.log

#2000, 1725, 1575, 1425, 1275, 1000
#  contrast: 45
#  hue: 45
#  saturation: 40
#  luminance: 50

#channel 7
#if [ $1 -eq 7 ]; then
    if [ $2 -lt 1050 ]; then
		yaml-cli -s .image.contrast 45
		yaml-cli -s .image.hue 45
		yaml-cli -s .image.saturation 40
		yaml-cli -s .image.luminance 50
		yaml-cli -s .isp.drc 320
		killall -1 majestic
      
    elif [ $2 -gt 1250 ] && [ $2 -lt 1300 ]; then
		yaml-cli -s .image.contrast 55
		yaml-cli -s .image.hue 50
		yaml-cli -s .image.saturation 50
		yaml-cli -s .image.luminance 50
		killall -1 majestic
	elif [ $2 -gt 1400 ] && [ $2 -lt 1450 ]; then
		yaml-cli -s .image.contrast 65
		yaml-cli -s .image.hue 50
		yaml-cli -s .image.saturation 50
		yaml-cli -s .image.luminance 60
		killall -1 majestic
    elif [ $2 -gt 1550 ] && [ $2 -lt 1600 ]; then
		yaml-cli -s .image.contrast 70
		yaml-cli -s .image.hue 50
		yaml-cli -s .image.saturation 55
		yaml-cli -s .image.luminance 70
	 
		killall -1 majestic
    elif [ $2 -gt 1700 ] && [ $2 -lt 1750 ]; then
		yaml-cli -s .image.contrast 80
		yaml-cli -s .image.hue 50
		yaml-cli -s .image.saturation 55
		yaml-cli -s .image.luminance 75
		killall -1 majestic
	elif [ $2 -gt 1950 ]; then
		yaml-cli -s .image.contrast 100
		yaml-cli -s .image.hue 60
		yaml-cli -s .image.saturation 60
		yaml-cli -s .image.luminance 80
		yaml-cli -s .isp.drc 150
		killall -1 majestic
    else
		echo unknown value $2
    fi
#fi


exit 1

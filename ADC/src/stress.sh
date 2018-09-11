while :
do
	(echo "0: " $(cat /dev/adc0)) &
	(echo "1: " $(cat /dev/adc1)) &
	(echo "2: " $(cat /dev/adc2)) &
done
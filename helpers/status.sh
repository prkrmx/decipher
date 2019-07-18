#!/bin/bash 

# set -e

usage="Get general system status

$(basename "$0") [-h] [-s] [-r n]
where:
	-h	show this help text
	-s	stop the process
	-r	run the process
	"

help="$(basename "$0") -r IP PORT ST
example:

sudo ./$(basename "$0") -r 10.255.255.255 13000 1
where position:
    -2  --IP    UDP IP address [default = 10.255.255.255]
    -3  --PORT  UDP PORT [default = 13000]
    -4  --ST	SLEEP TIME [default = 1 sec]

sudo ./$(basename "$0") -s
"

while getopts ':hsr' option; do
	case "$option" in
		h) # Print Help
			echo "$usage"
			echo "$help"
			exit
			;;
		r)
			ip=$2	# Broadcast IP
			port=$3	# Broadcast Port
			st=$4	# Sleep Time

			if [ -z $ip ];then
				ip=10.255.255.255
			fi
			if [ -z $port ];then
				port=13000
			fi
			if [ -z $st ];then
				st=1
			fi

			# Host name
			host=$(hostname)


			while true; do
				sleep $st
				
				paplon=0
				oclvankus=0
				delta_client=0


				# paplon status
				if pgrep -x "paplon.py" > /dev/null 
				then
					paplon=1
				fi
				
				# oclvankus status
				if pgrep -x "oclvankus.py" > /dev/null 
				then
					oclvankus=1
				fi

				# delta_client status
				if pgrep -x "delta_client.py" > /dev/null
				then
					delta_client=1
				fi


				# CPU utilization %
				cpu=$(grep 'cpu ' /proc/stat |awk '{printf "%.2f\n", ($2+$4)*100/($2+$4+$5)}')

				# RAM utilization (total used)
				ram=$(free -m |grep Mem: |awk '{print $3}')

				# CPU temperature C
				temp=$(sensors |grep 'Physical\|Package' |awk '{print $4+0}')

				# GPU temperature C
				gpu=$(aticonfig --odgt |awk 'FNR == 3 {print $5}')

				printf "DC|%s|%s|%s|%s|%s|%s|%s|%s" "$host" "$paplon" "$oclvankus" "$delta_client" "$cpu" "$ram" "$temp" "$gpu" | socat - UDP-DATAGRAM:$ip:$port,broadcast

			done
			exit 1
			;;
		s)
			echo "Kill the process..."
			killall $(basename "$0")
			exit
			;;
		:) 
			echo "Errors"
			printf "missing argument for -%s\n" "$OPTARG" >&2
			echo "$usage" >&2
			exit 1
			;;
		\?)
			echo "Errors"
			printf "illegal option: -%s\n" "$OPTARG" >&2
			echo "$usage" >&2
			exit 1
			;;
	esac
done
shift $((OPTIND - 1))
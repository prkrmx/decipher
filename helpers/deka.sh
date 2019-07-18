#!/bin/bash 

set -e

usage="$(basename "$0") [-h] [-s] [-r]
where:
	-h	show this help text
	-s	stop the process
	-r	run the process"

help="
expl:	
	Start Deka
	sudo ./$(basename "$0") -r

	Stop Deka
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
			echo "Starting Deka..."
			if pgrep -x "paplon.py" > /dev/null 
			then
				echo "Kill Paplon"
				kill -9 $(ps aux | grep paplon.py | grep -v "grep" | awk '{print $2}')
			fi

			if pgrep -x "oclvankus.py" > /dev/null 
			then
				echo "Kill Oclvankus"
				kill -9 $(ps aux | grep oclvankus.py | grep -v "grep" | awk '{print $2}')
			fi

			if pgrep -x "delta_client.py" > /dev/null 
			then
				echo "Kill Delta Client"
				kill -9 $(ps aux | grep delta_client.py | grep -v "grep" | awk '{print $2}')
			fi

			sleep 1

			cd  /home/mainuser/deka
			echo "Running Paplon"
			./paplon.py &

			echo "Running Oclvankus"
			# ./oclvankus.py <<< "0,1" &
			echo "0,1" | ./oclvankus.py &

			echo "Running Delta Client"
			sudo ./delta_client.py &
			exit
			;;
		s)
			echo "Kill the process..."

			if pgrep -x "paplon.py" > /dev/null 
			then
				echo "Kill Paplon"
				kill -9 $(ps aux | grep paplon.py | grep -v "grep" | awk '{print $2}')
			fi

			if pgrep -x "oclvankus.py" > /dev/null 
			then
				echo "Kill Oclvankus"
				kill -9 $(ps aux | grep oclvankus.py | grep -v "grep" | awk '{print $2}')
			fi

			if pgrep -x "delta_client.py" > /dev/null 
			then
				echo "Kill Delta Client"
				kill -9 $(ps aux | grep delta_client.py | grep -v "grep" | awk '{print $2}')
			fi

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
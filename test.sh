#!/bin/bash

if [ $# -lt 3 ]; then
	echo "Usage: bash script [test type] [ip] [port]"
	echo "test types: qemu-info, qemu-iso, nbdc-con, nbdc-disc, nbd-list"
	exit
fi
if [ $# -eq 3 ]; then
	if [[ "$1" == "qemu-info" ]]; then
		echo "qemu-img info test";
		read -p "Enter nbd-server export's name: " exportname		
		qemu-img info nbd:$2:$3:exportname=$exportname
		exit
	fi
	if [[ "$1" == "qemu-iso" ]]; then
		echo "qemu-system-x86_64 start OS test"
		read -p "Enter nbd-server export's name: " exportname		
		sudo qemu-system-x86_64 -cpu host -enable-kvm -drive file=nbd:$2:$3:exportname=$exportname
		exit
	fi
	if [[ "$1" == "nbdc-con" ]]; then
		echo "nbd-client connection test";
		read -p "Enter nbd-device (like /dev/nbdN after 'modprobe nbd'): " device
		read -p "Enter nbd-server export's name: " exportname		
		sudo nbd-client $2 $3 $device -N $exportname 
		exit
	fi
	if [[ "$1" == "nbdc-disc" ]]; then
		echo "nbd-client disconnection test";
		read -p "Enter nbd-device (like /dev/nbdN after 'modprobe nbd'): " device
		sudo nbd-client -d $device
		exit
	fi
	if [[ "$1" == "nbdc-list" ]]; then
		echo "nbd-client list test";
		nbd-client -l $2 $3
		exit
	else
		echo "incorrect test type"
		exit
	fi
else
	echo "too much arguments"
	exit
fi

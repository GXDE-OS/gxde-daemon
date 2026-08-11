#!/bin/bash

led_file=$(ls /sys/class/leds/input*::numlock/brightness 2>/dev/null | head -n1)
if [[ -z "$led_file" ]]; then
	echo "Unsupport"
	exit 1
fi

status=$(cat "$led_file" 2>/dev/null)
if [[ "$status" == "1" ]]; then
	dbus-send --session --dest=com.deepin.dde.osd \
	  --type=method_call / com.deepin.dde.osd.ShowOSD \
	  string:"NumLockOn"
else
	dbus-send --session --dest=com.deepin.dde.osd \
	  --type=method_call / com.deepin.dde.osd.ShowOSD \
	  string:"NumLockOff"
fi

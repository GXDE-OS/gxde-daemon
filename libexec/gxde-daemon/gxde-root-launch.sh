#!/bin/bash
if [[ ! -e /var/run/dbus/system_bus_socket ]]; then
dbus-daemon --system
fi
# 开启 gxde-k9 
setsid /usr/bin/gxde-k9 &

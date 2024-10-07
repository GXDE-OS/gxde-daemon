#!/bin/bash
dbus-daemon --system
# 开启 pkexec 授权服务
setsid /usr/lib/polkit-1/polkitd &

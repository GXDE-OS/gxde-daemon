#!/usr/bin/env python3
import os
import json
import pydbus 
import subprocess
import gi.repository

homePath = os.getenv("HOME")

def ChrootPath():
    return subprocess.getoutput("readlink -f /proc/self/root")

loop = gi.repository.GLib.MainLoop()

class SystemInfo(object):
    """    
        <node>
            <interface name='com.gxde.daemon.system.info'>
                <method name='IsInChroot'>
                    <arg type='b' name='action' direction='out'/>
                </method>
            </interface>
        </node>
    """

    def IsInChroot(self):
        return ChrootPath() != "/"


bus = pydbus.SessionBus()
bus.publish("com.gxde.daemon.system.info", SystemInfo())
loop.run()
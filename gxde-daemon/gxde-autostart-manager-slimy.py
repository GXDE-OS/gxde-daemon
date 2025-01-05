#!/usr/bin/env python3
import os
import json
import pydbus 
import subprocess
import gi.repository

homePath = os.getenv("HOME")

def ReadAutoStartConfigFilePathList():
    configList = []
    for x in ["/usr/share/gxde-k9/slimy",
              f"{homePath}/.local/share/GXDE/gxde-k9/slimy"]:
        for y in os.listdir(x):
            if (len(y) > 6 and ".slimy" == y[-6:]):
                configList.append(f"{x}/{y}")
    return configList

def ReadAutoStartConfigName():
    configPathList = ReadAutoStartConfigFilePathList()
    configList = []
    for i in configPathList:
        configList.append(os.path.splitext(os.path.basename(i))[0])
    return configList


loop = gi.repository.GLib.MainLoop()

class AutoStartManager(object):
    """    
        <node>
            <interface name='com.gxde.daemon.autostart.manager.slimy'>
                <method name='RunProgram'>
                    <arg type='s' name='action' direction='in'/>
                </method>
                <method name='ListProgramConfigFilePath'>
                    <arg type='as' name='action' direction='out'/>
                </method>
                <method name='ListProgramConfigName'>
                    <arg type='as' name='action' direction='out'/>
                </method>
            </interface>
        </node>
    """

    def ListProgramConfigName(self):
        return ReadAutoStartConfigName()

    def ListProgramConfigFilePath(self):
        return ReadAutoStartConfigFilePathList()
    
    def RunProgram(self, s):
        os.popen(s)


bus = pydbus.SessionBus()
bus.publish("com.gxde.daemon.autostart.manager.slimy", AutoStartManager())
loop.run()
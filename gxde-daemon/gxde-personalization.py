#!/usr/bin/env python3
import os
import shutil
import pydbus 
import subprocess
import gi.repository

homePath = os.getenv("HOME")
configDirPath = f"{homePath}/.local/share/GXDE/gxde-k9/slimy/"
configSourcePath = "/usr/share/gxde-daemon/daemon-k9-conf/slimy/"

def IsAutoStartConfigExists(name: str):
    return (os.path.exists(f"{homePath}/.config/autostart/{name}.desktop") or 
            os.path.exists(f"{homePath}/.local/share/GXDE/gxde-k9/slimy/{name}.slimy"))

def SetAutoStartConfig(name: str, enable: bool):
    # 删除旧的自启动文件
    if (os.path.exists(f"{homePath}/.config/autostart/{name}.desktop")):
        os.remove(f"{homePath}/.config/autostart/{name}.desktop")
    if (os.path.exists(f"{configDirPath}/{name}.slimy")):
        os.remove(f"{configDirPath}/{name}.slimy")

    if (enable):
        shutil.copy(f"{configSourcePath}/{name}.slimy",
                    f"{configDirPath}/{name}.slimy")
        os.system(f"setsid '{name}' > /dev/null 2>&1 &")
        return
    os.system(f"killall '{name}'")

loop = gi.repository.GLib.MainLoop()

class AutoStartManager(object):
    """    
        <node>
            <interface name='com.gxde.daemon.personalization'>
                <method name='IsOpenTopPanelGlobalMenu'>
                    <arg type='b' name='action' direction='out'/>
                </method>
               <method name='IsOpenTopPanel'>
                    <arg type='b' name='action' direction='out'/>
                </method>
                <method name='IsOpenBottomPanel'>
                    <arg type='b' name='action' direction='out'/>
                </method>
                <method name='SetTopPanel'>
                    <arg type='b' name='action' direction='in'/>
                </method>
                <method name='SetTopPanelGlobalMenu'>
                    <arg type='b' name='action' direction='in'/>
                </method>
                <method name='SetBottomPanel'>
                    <arg type='b' name='action' direction='in'/>
                </method>
            </interface>
        </node>
    """

    def IsOpenTopPanelGlobalMenu(self):
        return IsAutoStartConfigExists("gxde-globalmenu-service")
    
    def IsOpenTopPanel(self):
        return IsAutoStartConfigExists("gxde-top-panel")
        
    def IsOpenBottomPanel(self):
        return IsAutoStartConfigExists("plank")
    
    def SetTopPanel(self, isTopPanel: bool):
        SetAutoStartConfig("gxde-top-panel", isTopPanel)

    def SetTopPanelGlobalMenu(self, value: bool):
        SetAutoStartConfig("gxde-globalmenu-service", value)

    def SetBottomPanel(self, value: bool):
        SetAutoStartConfig("plank", value)


bus = pydbus.SessionBus()
bus.publish("com.gxde.daemon.personalization", AutoStartManager())
loop.run()
#!/usr/bin/env python3
import os
import pydbus 
import subprocess
import gi.repository

loop = gi.repository.GLib.MainLoop()

homePath = os.getenv("HOME")
configPath = f"{homePath}/.config/GXDE/gxde-requ/gxde-requ-setting.qaq"

def ReadCornerEdge(place):
    if (not os.path.exists(configPath)):
        return "a"
    edgeAction = ""
    with open(configPath, "r") as file:
        for i in file.read().splitlines():
            if (place in i.split("=")[0]):
                edgeAction = i.split("=")[1]
    return edgeAction

def SetCornerEdge(place, action):
    # 支持 TopLeft、TopRight、LowerLeft 和 LowerRight
    subprocess.Popen(["gxde-requ-setter", "--set", place, action])

class ScreenEdge(object):
    """    
        <node>
            <interface name='com.gxde.corneredge'>
                <method name='SetBottomLeft'>
                    <arg type='s' name='action' direction='in'/>
                </method>
                <method name='SetBottomRight'>
                    <arg type='s' name='action' direction='in'/>
                </method>
                <method name='SetTopLeft'>
                    <arg type='s' name='action' direction='in'/>
                </method>
                <method name='SetTopRight'>
                    <arg type='s' name='action' direction='in'/>
                </method>

                <method name='BottomLeftAction'>
                    <arg type='s' name='action' direction='out'/>
                </method>
                <method name='BottomRightAction'>
                    <arg type='s' name='action' direction='out'/>
                </method>
                <method name='TopLeftAction'>
                    <arg type='s' name='action' direction='out'/>
                </method>
                <method name='TopRightAction'>
                    <arg type='s' name='action' direction='out'/>
                </method>
            </interface>
        </node>
    """
    
    def SetBottomLeft(self, s):
        os.system("killall gxde-requ -9")
        return SetCornerEdge("LowerLeft", s)
    
    def SetBottomRight(self, s):
        os.system("killall gxde-requ -9")
        return SetCornerEdge("LowerRight", s)

    def SetTopLeft(self, s):
        os.system("killall gxde-requ -9")
        return SetCornerEdge("TopLeft", s)

    def SetTopRight(self, s):
        os.system("killall gxde-requ -9")
        return SetCornerEdge("TopRight", s)
    
    def TopLeftAction(self):
        return ReadCornerEdge("TopLeft")

    def TopRightAction(self):
        return ReadCornerEdge("TopRight")
    
    def BottomLeftAction(self):
        return ReadCornerEdge("LowerLeft")

    def BottomRightAction(self):
        return ReadCornerEdge("LowerRight")
    

bus = pydbus.SessionBus()
bus.publish("com.gxde.corneredge", ScreenEdge())
loop.run()
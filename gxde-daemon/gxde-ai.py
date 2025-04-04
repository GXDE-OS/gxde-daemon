#!/usr/bin/env python3
import os
import pydbus 
import subprocess
import gi.repository
import utils.ConfigManager

m_homePath = os.getenv("HOME")
# 检测支持的语音引擎
supportList = []
if (not (os.system("which edge-playback") >> 8)):
    supportList.append("edge-tts")
if (not (os.system("which espeak") >> 8)):
    supportList.append("espeak")

m_configManager = utils.ConfigManager.ConfigManager("com.gxde.daemon.ai.speaker")
if (m_configManager.read("UseSpeaker") == None and len(supportList) > 0):
    m_configManager.write("UseSpeaker", supportList[0])

loop = gi.repository.GLib.MainLoop()

class AISpeaker(object):
    """    
        <node>
            <interface name='com.gxde.daemon.ai.speaker'>
                <method name='TextToSpeech'>
                    <arg type='s' name='action' direction='in'/>
                </method>
                <method name='SpeakerList'>
                    <arg type='as' name='action' direction='out'/>
                </method>
                <method name='SetSpeaker'>
                    <arg type='s' name='action' direction='in'/>
                </method>
                <method name='UseSpeaker'>
                    <arg type='s' name='action' direction='out'/>
                </method>
            </interface>
        </node>
    """

    def TextToSpeech(self, text: str):
        speaker = self.UseSpeaker()
        if (speaker == "edge-tts"):
            subprocess.run(["edge-playback", "--voice", "zh-CN-XiaoyiNeural", 
                        "--text", text])
            return
        if (speaker == "espeak"):
            subprocess.run(["espeak", "-v", "zh", text])

    def SpeakerList(self):
        return supportList
    
    def SetSpeaker(self, speaker):
        if (not speaker in supportList):
            raise ValueError("只支持 dbus 接口 SpeakerList 内的 Speaker")
        m_configManager.write("UseSpeaker", speaker)

    def UseSpeaker(self):
        return m_configManager.read("UseSpeaker")

bus = pydbus.SessionBus()
bus.publish("com.gxde.daemon.ai.speaker", AISpeaker())
loop.run()
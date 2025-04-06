#!/usr/bin/env python3
import os
import pydbus 
import subprocess
import gi.repository
import utils.ConfigManager

m_homePath = os.getenv("HOME")

m_configManager = utils.ConfigManager.ConfigManager("com.gxde.daemon.ai")

# 检测支持的语音引擎
supportSpeakerList = []
if (not (os.system("which edge-playback") >> 8)):
    supportSpeakerList.append("edge-tts")
if (not (os.system("which espeak") >> 8)):
    supportSpeakerList.append("espeak")


if (m_configManager.read("UseSpeaker") == None and len(supportSpeakerList) > 0):
    m_configManager.write("UseSpeaker", supportSpeakerList[0])

supportTranslateEngineList = []
# 获取翻译支持的 Engine
if (not (os.system("which trans") >> 8)):
    supportTranslateEngineList = subprocess.getoutput("trans --list-engines").replace("*", "").replace("  ", "").splitlines()
if (m_configManager.read("UseTranslateEngine") == None and len(supportTranslateEngineList) > 0):
    if ("bing" in supportTranslateEngineList):
        m_configManager.write("UseTranslateEngine", "bing")
    else:
        m_configManager.write("UseTranslateEngine", supportTranslateEngineList[0])

loop = gi.repository.GLib.MainLoop()

class AITranslate(object):
    """    
        <node>
            <interface name='com.gxde.daemon.ai.translate'>
                <method name='EngineList'>
                    <arg type='as' name='action' direction='out'/>
                </method>
                <method name='LanguageCodeList'>
                    <arg type='as' name='action' direction='out'/>
                </method>
                <method name='LanguageNameList'>
                    <arg type='as' name='action' direction='out'/>
                </method>
                <method name='LanguageEnglishNameList'>
                    <arg type='as' name='action' direction='out'/>
                </method>
                <method name='UseEngine'>
                    <arg type='s' name='action' direction='out'/>
                </method>
                <method name='Translate'>
                    <arg type='s' name='action' direction='in'/>
                    <arg type='s' name='action' direction='in'/>
                    <arg type='s' name='action' direction='out'/>
                </method>
            </interface>
        </node>
    """

    def EngineList(self):
        return supportTranslateEngineList
    
    def UseEngine(self):
        return m_configManager.read("UseTranslateEngine")
    
    def Translate(self, text, to_code):
        if (len(supportTranslateEngineList) <= 0):
            return None
        result = subprocess.run(["trans", "-e", m_configManager.read("UseTranslateEngine"),
                        "-b", ":" + to_code, text],
                        check=True, capture_output=True,
                        timeout=10)
        return result.stdout.decode()
    
    def LanguageCodeList(self):
        if (len(supportTranslateEngineList) <= 0):
            return None
        return subprocess.getoutput("trans -list-codes").splitlines()
    
    def LanguageNameList(self):
        if (len(supportTranslateEngineList) <= 0):
            return None
        return subprocess.getoutput("trans -list-languages").splitlines()
    
    def LanguageEnglishNameList(self):
        if (len(supportTranslateEngineList) <= 0):
            return None
        return subprocess.getoutput("trans -list-languages-english").splitlines()

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
        return supportSpeakerList
    
    def SetSpeaker(self, speaker):
        if (not speaker in supportSpeakerList):
            raise ValueError("只支持 dbus 接口 SpeakerList 内的 Speaker")
        m_configManager.write("UseSpeaker", speaker)

    def UseSpeaker(self):
        return m_configManager.read("UseSpeaker")

bus = pydbus.SessionBus()
bus.publish("com.gxde.daemon.ai.speaker", AISpeaker())
bus.publish("com.gxde.daemon.ai.translate", AITranslate())
loop.run()
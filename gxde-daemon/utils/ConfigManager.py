import os
import json
import traceback

class ConfigManager:
    homePath = os.getenv("HOME")
    configDirPath = f"{homePath}/.config/gxde-daemon"
    program = ""
    configFilePath = f"{configDirPath}/{program}.json"
    configData = {}

    def __init__(self, program):
        # 创建配置文件
        if (not os.path.exists(self.configDirPath)):
            os.makedirs(self.configDirPath)
        self.program = program
        self.configFilePath = f"{self.configDirPath}/{program}.json"

    def refreshConfig(self):
        if (os.path.exists(self.configFilePath)):
            self.configData = self.FileReader.readJson(self.configFilePath)
            return
        self.configData = {}

    def saveConfig(self):
        self.FileReader.writeJson(self.configFilePath, self.configData)

    def read(self, item):
        if (self.configData == {}):
            self.refreshConfig()
        if item in self.configData:
            return self.configData[item]
        return None
        
    def write(self, item, data):
        self.configData[item] = data
        self.saveConfig()

    def remove(self, item):
        del self.configData[item]

    class FileReader:
        @staticmethod
        def read(path):
            with open(path, "r") as file:
                data = file.read()
            return data
        
        @staticmethod
        def write(path, data):
            with open(path, "w") as file:
                file.write(data)
        
        @staticmethod
        def readJson(path):
            return json.loads(ConfigManager.FileReader.read(path))
        
        @staticmethod
        def writeJson(path, data):
            ConfigManager.FileReader.write(
                path, 
                json.dumps(
                    data, 
                    ensure_ascii=False, 
                    indent=4))
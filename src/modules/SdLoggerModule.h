#pragma once

#include "MeshModule.h"
#include "SinglePortModule.h"

/// here probably will be more checks
#if defined(HAS_SDCARD) /// && defined(ARCH_ESP32) 

class SdLoggerModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    SdLoggerModule();

    static void listSDFiles(const char * dirname, uint8_t levels);
    static void writeFile(const char * path, const char * message);
    static void createSDDir(const char * path);
    static void appendSDFile(const char * path, const char * message);
    static void readSDFile(const char * path, std::vector<uint8_t> &fileData);

  protected:
    virtual int32_t runOnce() override;

  private:
    static const char * moduleName;
    static const char * moduleThread;
    static const char * logsPath;
    static constexpr meshtastic_PortNum fakePortNumber = meshtastic_PortNum_UNKNOWN_APP;
    static constexpr int64_t LOG_PERIOD_MS = 5000;

    int64_t lastLogTime = millis();
    std::string currentDate;

    void logCurrentState();
    std::string generateGpsLog(std::string &dateString) const;
    std::string generateDeviceInfoLog() const;
    static std::string toStringWithZeros(const int value, const size_t numberOfDigits);
};

extern SdLoggerModule *sdLoggerModule;

#endif
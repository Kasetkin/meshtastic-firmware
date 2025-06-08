#include "SdLoggerModule.h"
#include "Telemetry/EnvironmentTelemetry.h"
#include "FSCommon.h"
#include "SPILock.h"
#include "RTC.h"
#include "configuration.h"

#include <SD.h>
#include <SPI.h>

SdLoggerModule *sdLoggerModule;

const char * SdLoggerModule::moduleName = "SdLogger";
const char * SdLoggerModule::moduleThread = "SdLogger";
const char * SdLoggerModule::logsPath = "/logs";

SdLoggerModule::SdLoggerModule() : SinglePortModule(moduleName, fakePortNumber), concurrency::OSThread(moduleThread) 
{
    LOG_DEBUG("SdLoggerModule | CTOR");
}

int32_t SdLoggerModule::runOnce()
{
    const unsigned long thisMoment = millis();
    LOG_DEBUG("SdLoggerModule | runOnce, time is %d", thisMoment);

    if (thisMoment < lastLogTime + LOG_PERIOD_MS) {
        const unsigned long timeToSleep = LOG_PERIOD_MS + lastLogTime - thisMoment + 1;
        LOG_DEBUG("too early, sleep for %d millisec", timeToSleep);
        return static_cast<int32_t>(timeToSleep);
    }

    logCurrentState();

    return static_cast<int32_t>(LOG_PERIOD_MS);
}

std::string SdLoggerModule::dopToMeters(const uint32_t dop)
{
    const auto dv = std::div(dop, 100);
    return std::to_string(dv.quot) + '.' + std::to_string(dv.rem);
}

std::string SdLoggerModule::toStringWithZeros(const int value, const size_t numberOfDigits)
{
    const std::string basicString = std::to_string(value);
    if (numberOfDigits > basicString.size())
        return std::string(numberOfDigits - basicString.size(), '0') + basicString;
    else
        return basicString;
}

std::string SdLoggerModule::toTelemetryRoundedString(const float value)
{
    std::string fullString = std::to_string(value);
    const size_t dotPos = fullString.find('.');
    if (dotPos == std::string::npos)
        return fullString;

    const size_t newLenght = std::min(dotPos + static_cast<size_t>(4), fullString.size());
    fullString.resize(newLenght);
    return fullString;
}

void SdLoggerModule::logCurrentState()
{
    LOG_DEBUG("SdLoggerModule | message generation - start");
    createSDDir(logsPath);

    const std::string filename = generateFilename() + ".csv";
    const std::string deviceLog = generateDeviceInfoLog();
    const std::string gpsLog = generateGpsLog();
    const std::string envTelemetry = generateTelemetryLog();

    const std::string fullLogMessage = deviceLog + gpsLog + envTelemetry + std::string("\n");
    LOG_DEBUG("SdLoggerModule | message generation - end");
    LOG_DEBUG("SdLoggerModule | full message: \\");
    LOG_DEBUG("%s \\",  deviceLog.c_str());
    LOG_DEBUG("%s \\",  gpsLog.c_str());
    LOG_DEBUG("%s \\",  envTelemetry.c_str());
    LOG_DEBUG("END-OF-LINE");

    const std::string fullpath = std::string(logsPath) + "/" + filename;
    appendSDFile(fullpath.c_str(), fullLogMessage.c_str());

}

std::string SdLoggerModule::generateTelemetryLog() const
{
    LOG_DEBUG("SdLoggerModule | generate telemetry - start");
    if (environmentTelemetryModule == nullptr) {
        LOG_DEBUG("SdLoggerModule | generate telemetry - abort: no Telemetry module");
        return "";
    }

    // const bool moduleEnabled = environmentTelemetryModule->enabled;

    /// BAD CODE !!!
    /// we actually READ new telemetry from sensors
    //! \todo read last saved data from TelemetryModule
    meshtastic_Telemetry m = meshtastic_Telemetry_init_zero;
    const bool readTelemetryResult = environmentTelemetryModule->getEnvironmentTelemetry(&m);
    if (!readTelemetryResult)
        LOG_DEBUG("FALSE from telemetry module");

    auto &envTelemetry = m.variant.environment_metrics;
    const char * logValue = envTelemetry.has_temperature ? "TRUE" : "FALSE";
    LOG_DEBUG("telemetry: time %d; variant %d, temp %s %f",
        m.time, m.which_variant, logValue, envTelemetry.temperature);

    std::string result;
    if (envTelemetry.has_temperature)
        result += std::string("TEMP;") + toTelemetryRoundedString(envTelemetry.temperature) + std::string(";");

    if (envTelemetry.has_relative_humidity)
        result += std::string("HUMID;") + toTelemetryRoundedString(envTelemetry.relative_humidity) + std::string(";");

    if (envTelemetry.barometric_pressure != 0)
        result += std::string("PRESS;") + toTelemetryRoundedString(envTelemetry.barometric_pressure) + std::string(";");

    // /// remove last ";" if there is any
    // const size_t resultSize = result.size();
    // if (resultSize > 0)
    //     result.resize(resultSize - 1);

    LOG_DEBUG("SdLoggerModule | generate telemetry - end | result: %s", result.c_str());
    return result;
}

std::string SdLoggerModule::generateDeviceInfoLog() const
{
    LOG_DEBUG("SdLoggerModule | generate device info - start");
    const auto &ownerId = devicestate.owner.id;
    const auto &ownerShortName = devicestate.owner.short_name;
    const auto &ownerFullName = devicestate.owner.long_name;

    const std::string message =
            std::string("ID;") + std::string(ownerId)
        + std::string(";NAME;") + std::string(ownerShortName)
        + std::string(";FULLNAME;") + std::string(ownerFullName)
        + std::string(";");

    LOG_DEBUG("SdLoggerModule | generate device info - end | result: %s", message.c_str());
    return message;
}

std::string SdLoggerModule::generateFilename() const
{
    const bool requestLocalTime = false;
    uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityDevice, requestLocalTime);
    if (rtc_sec == 0)
        return "NO-DATE-FILE";

    struct tm  gmTime{};
    const time_t stampT = static_cast<time_t>(rtc_sec);
    gmTime = *gmtime(&stampT);

    constexpr int GMTIME_YEAR_FIX = 1900;
    constexpr int GMTIME_MONTH_FIX = 1;
    gmTime.tm_year += GMTIME_YEAR_FIX;
    gmTime.tm_mon += GMTIME_MONTH_FIX;

    const std::string yearStr = std::to_string(gmTime.tm_year);
    const std::string monthStr = toStringWithZeros(gmTime.tm_mon, 2);
    const std::string dayStr = toStringWithZeros(gmTime.tm_mday, 2);
    std::string dateString = yearStr + "-" + monthStr + "-" + dayStr;

    LOG_DEBUG("timestamp from RTC: %d, date string: %s", rtc_sec, dateString.c_str());
    return dateString;
}

std::string SdLoggerModule::generateGpsLog() const
{
    LOG_DEBUG("SdLoggerModule | generate GPS info - start");
    const auto &p = localPosition;

    struct tm  gmTime{};
    const time_t stampT = static_cast<time_t>(p.timestamp);
    gmTime = *gmtime(&stampT);

    constexpr int GMTIME_YEAR_FIX = 1900;
    constexpr int GMTIME_MONTH_FIX = 1;
    gmTime.tm_year += GMTIME_YEAR_FIX;
    gmTime.tm_mon += GMTIME_MONTH_FIX;

    const std::string yearStr = std::to_string(gmTime.tm_year);
    const std::string monthStr = toStringWithZeros(gmTime.tm_mon, 2);
    const std::string dayStr = toStringWithZeros(gmTime.tm_mday, 2);
    const std::string dateString = yearStr + "-" + monthStr + "-" + dayStr;

    const std::string hoursStr = toStringWithZeros(gmTime.tm_hour, 2);
    const std::string minutesStr = toStringWithZeros(gmTime.tm_min, 2);
    const std::string secondsStr = toStringWithZeros(gmTime.tm_sec, 2);
    const std::string millisWithZeros = p.timestamp_millis_adjust > 0
        ? '.' + toStringWithZeros(p.timestamp_millis_adjust, 3)
        : std::string();

    const std::string timeString = hoursStr + ":" + minutesStr + ":" + secondsStr + millisWithZeros;
    const std::string dateTimeStringFull = dateString + 'T' + timeString + 'Z';

    // LOG_DEBUG("date from GPS: %d-%d-%dT%d:%d:%d.%dZ",
    //     gmTime.tm_year, gmTime.tm_mon, gmTime.tm_mday,
    //     gmTime.tm_hour, gmTime.tm_min, gmTime.tm_sec,
    //     p.timestamp_millis_adjust
    // );
    // LOG_DEBUG("date formatted by code: %s", dateTimeStringFull.c_str());

    const double lat = static_cast<double>(p.latitude_i) * 1e-7;
    const double lon = static_cast<double>(p.longitude_i) * 1e-7;
    const std::string message =
        std::string("DT;") + dateTimeStringFull
        + std::string(";LAT;") + std::to_string(lat)
        + std::string(";LON;") + std::to_string(lon)
        + std::string(";ALT;") + std::to_string(p.altitude)
        + std::string(";SATS;") + std::to_string(p.sats_in_view)
        + std::string(";HDOP;") + dopToMeters(p.HDOP)
        // + std::string(";VDOP;") + dopToMeters(p.VDOP)
        + std::string(";");

    LOG_DEBUG("SdLoggerModule | generate GPS info - end");
    return message;
}

void SdLoggerModule::listSDFiles(const char * dirname, uint8_t levels)
{
    concurrency::LockGuard g(spiLock);
    LOG_DEBUG("Listing directory: %s\n", dirname);

    const bool cardIsReady = testAndInitSDCard();
    if (!cardIsReady)
        return;

    File root = SD.open(dirname);
    if(!root){
        LOG_DEBUG("Failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        LOG_DEBUG("Not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file){
        if (file.isDirectory()) {
            LOG_DEBUG("  DIR : %s", file.name());
            if(levels)
                listDir(file.name(), levels - 1);
    } else {
        LOG_DEBUG("  FILE: %s  SIZE: %lu", file.name(), file.size());
    }
        file = root.openNextFile();
    }
}

void SdLoggerModule::writeFile(const char * path, const char * message)
{
    concurrency::LockGuard g(spiLock);
    LOG_DEBUG("Writing file: %s\n", path);

    const bool cardIsReady = testAndInitSDCard();
    if (!cardIsReady)
        return;

    File file = SD.open(path, FILE_WRITE);
    if (!file) {
        LOG_DEBUG("Failed to open file for writing");
        return;
    }

    if (file.print(message))
        LOG_DEBUG("File written");
    else
       LOG_DEBUG("Write failed");

    file.close();
}

void SdLoggerModule::createSDDir(const char * path)
{
    concurrency::LockGuard g(spiLock);

    const bool cardIsReady = testAndInitSDCard();
    if (!cardIsReady)
        return;

    if (SD.exists(path)) {
        LOG_DEBUG("Path: <%s> already exists, do nothing\n", path);
        return;
    }

    LOG_DEBUG("Creating Dir: %s\n", path);
    if (SD.mkdir(path))
        LOG_DEBUG("Dir created");
    else
        LOG_DEBUG("mkdir failed");
}

void SdLoggerModule::appendSDFile(const char * path, const char * message)
{
    concurrency::LockGuard g(spiLock);
    LOG_DEBUG("Appending to file: %s\n", path);

    const bool cardIsReady = testAndInitSDCard();
    if (!cardIsReady)
        return;

    File file = SD.open(path, FILE_APPEND);
    if (!file){
        LOG_DEBUG("Failed to open file for appending");
        return;
    }

    if (file.print(message))
        LOG_DEBUG("Message appended");
    else
        LOG_DEBUG("Append failed");

    file.close();
}

void SdLoggerModule::readSDFile(const char * path, std::vector<uint8_t> &fileData)
{
    concurrency::LockGuard g(spiLock);
    LOG_DEBUG("Reading file: %s\n", path);

    fileData.clear();

    const bool cardIsReady = testAndInitSDCard();
    if (!cardIsReady)
        return;

    File file = SD.open(path);
    if(!file) {
        LOG_DEBUG("Failed to open file for reading");
        return;
    }

    fileData.reserve(file.size());

    LOG_DEBUG("Read from file: ");

    constexpr size_t MAX_BUFFER_SIZE = 256;
    std::array<uint8_t, MAX_BUFFER_SIZE> readBuffer;
    const uint16_t blockSize = readBuffer.size();
    while(file.available()) {
        const int realBlockSize = file.read(readBuffer.data(), blockSize);
        std::copy(readBuffer.cbegin(), readBuffer.cbegin() + realBlockSize, std::back_inserter(fileData));
        LOG_DEBUG("read new %d bytes from the file", realBlockSize);
    }

    fileData.shrink_to_fit();
    file.close();
}
#include "SdLoggerModule.h"
#include "FSCommon.h"
#include "SPILock.h"
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
    const int64_t thisMoment = static_cast<int64_t>(millis());
    LOG_DEBUG("SdLoggerModule | runOnce, time is %d", thisMoment);

    if (thisMoment < lastLogTime + LOG_PERIOD_MS) {
        const int64_t timeToSleep = LOG_PERIOD_MS + lastLogTime - thisMoment + 1;
        LOG_DEBUG("too early, sleep for %d millisec", timeToSleep);
        return static_cast<int32_t>(timeToSleep);
    }

    logCurrentState();

    return static_cast<int32_t>(LOG_PERIOD_MS);
}


std::string SdLoggerModule::toStringWithZeros(const int value, const size_t numberOfDigits)
{
    const std::string basicString = std::to_string(value);
    if (numberOfDigits > basicString.size())
        return std::string(numberOfDigits - basicString.size(), '0') + basicString;
    else
        return basicString;
}

void SdLoggerModule::logCurrentState()
{
    LOG_DEBUG("SdLoggerModule | message generation - start");
    createSDDir(logsPath);

    const std::string deviceLog = generateDeviceInfoLog();
    const std::string gpsLog = generateGpsLog(currentDate);

    const std::string fullLogMessage = deviceLog + ";" + gpsLog + "\n";
    LOG_DEBUG("SdLoggerModule | message generation - end");
    LOG_DEBUG("SdLoggerModule | full message: %s", fullLogMessage.c_str());

    const std::string filename = currentDate + ".csv";
    const std::string fullpath = std::string(logsPath) + "/" + filename;
    appendSDFile(fullpath.c_str(), fullLogMessage.c_str());

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
        + std::string(";FULLNAME;") + std::string(ownerFullName);

    LOG_DEBUG("SdLoggerModule | generate device info - end");
    return message;
}

std::string SdLoggerModule::generateGpsLog(std::string &dateString) const 
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
    dateString = yearStr + "-" + monthStr + "-" + dayStr;

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
        + std::string(";SATS;") + std::to_string(p.sats_in_view);

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
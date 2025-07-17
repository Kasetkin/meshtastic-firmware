#include "unicore.h"
#include <cctype>
#include <cmath>
#include "configuration.h"

PppInfo localPPP = PppInfo();

int32_t parseDegreesLatLon(const char *str)
{
    // LOG_DEBUG("parse decimal degree from sring [%s]", str);

    // An invalid character
    if (!isdigit(*str))
        return PPP_BAD_LATLON;

    const int32_t roundDigits = static_cast<int32_t>(atol(str));
    // LOG_DEBUG("left part of str is %d", roundDigits);
    if ((roundDigits < -181) || (roundDigits > 181))
        return PPP_BAD_LATLON;

    while (isdigit(*str))
        ++str;

    // Degree must have a decimal point
    if (*str != '.')
        return PPP_BAD_LATLON;
        
    /// skip '.'
    ++str;

    const int32_t meshtasticLatLonMultiplier = 1000 * 1000 * 10;
    int32_t currectDigitMultiplier = meshtasticLatLonMultiplier / 10;
    int32_t accumulator = 0;
    do
    {
        // LOG_DEBUG("digit is [%c], multiplier is [%d], accum is [%d]", *str, currectDigitMultiplier, accumulator);
        accumulator += (*str - '0') * currectDigitMultiplier;
        currectDigitMultiplier /= 10;
    } while (isdigit(*++str) && (currectDigitMultiplier > 0));

    // const uint64_t rightOfDecimal = static_cast<uint64_t>(atoll(str));
    // LOG_DEBUG("right part of str is %d", rightOfDecimal);
    
    // const int32_t result = leftOfDecimal * meshtasticLatLonMultiplier + rightOfDecimal;
    // LOG_DEBUG("result value is %d", result);

    const int32_t result = roundDigits * meshtasticLatLonMultiplier + accumulator;
    // LOG_DEBUG("result is [%d]", result);
    return result;
}

std::string prepareString(const char *str)
{
    std::string cppStr(str);
    /// first check and skip "17;" part of "17;SOL_COMPUTED" string
    const auto findResultIter = cppStr.find(';');
    if (findResultIter != std::string::npos)
        cppStr = cppStr.substr(findResultIter + 1);

    std::transform(cppStr.begin(), cppStr.end(), cppStr.begin(),
                   [](unsigned char c){ return std::toupper(c); } // correct
    );

    // LOG_DEBUG("original string: %s", str);
    // LOG_DEBUG("prepared string: %s", cppStr.c_str());
    return cppStr;
}

PppSolutionStatus parseSolutionStatus(const char *str)
{
    const std::string cppStr = prepareString(str);
    if (cppStr == "SOL_COMPUTED")
        return PppSolutionStatus::SOL_COMPUTED;
    else if (cppStr == "INSUFFICIENT_OBS")
        return PppSolutionStatus::INSUFFICIENT_OBS;
    else if (cppStr == "NO_CONVERGENCE")
        return PppSolutionStatus::NO_CONVERGENCE;
    else if (cppStr == "COV_TRACE")
        return PppSolutionStatus::COV_TRACE;
    else
        return PppSolutionStatus::NO_VALUE;

    return PppSolutionStatus::NO_VALUE;
}

PositionVelocityType parsePositionType(const char *str)
{
    const std::string cppStr = prepareString(str);
    if (cppStr == "NONE")
        return PositionVelocityType::NONE;
    else if (cppStr == "FIXEDPOS")
        return PositionVelocityType::FIXEDPOS;
    else if (cppStr == "FIXEDHEIGHT")
        return PositionVelocityType::FIXEDHEIGHT;
    else if (cppStr == "DOPPLER_VELOCITY")
        return PositionVelocityType::DOPPLER_VELOCITY;
    else if (cppStr == "SINGLE")
        return PositionVelocityType::SINGLE;
    else if (cppStr == "PSRDIFF")
        return PositionVelocityType::PSRDIFF;
    else if (cppStr == "SBAS")
        return PositionVelocityType::SBAS;
    else if (cppStr == "L1_FLOAT")
        return PositionVelocityType::L1_FLOAT;
    else if (cppStr == "IONOFREE_FLOAT")
        return PositionVelocityType::IONOFREE_FLOAT;
    else if (cppStr == "NARROW_FLOAT")
        return PositionVelocityType::NARROW_FLOAT;
    else if (cppStr == "L1_INT")
        return PositionVelocityType::L1_INT;
    else if (cppStr == "WIDE_INT")
        return PositionVelocityType::WIDE_INT;
    else if (cppStr == "NARROW_INT")
        return PositionVelocityType::NARROW_INT;
    else if (cppStr == "INS")
        return PositionVelocityType::INS;
    else if (cppStr == "INS_PSRSP")
        return PositionVelocityType::INS_PSRSP;
    else if (cppStr == "INS_PSRDIFF")
        return PositionVelocityType::INS_PSRDIFF;
    else if (cppStr == "INS_RTKFLOAT")
        return PositionVelocityType::INS_RTKFLOAT;
    else if (cppStr == "INS_RTKFIXED")
        return PositionVelocityType::INS_RTKFIXED;
    else if (cppStr == "PPP_CONVERGING")
        return PositionVelocityType::PPP_CONVERGING;
    else if (cppStr == "PPP")
        return PositionVelocityType::PPP;
    else
        return PositionVelocityType::NO_VALUE;

    return PositionVelocityType::NO_VALUE;
}

std::uint32_t CalculateCRC32(std::uint8_t *szBuf, int iSize)
{
    std::uint32_t ulCRC = 0;
    for (int iIndex = 0; iIndex < iSize; iIndex++)
        ulCRC = aulCrcTable[(ulCRC ^ szBuf[iIndex]) & 0xff] ^ (ulCRC >> 8);

    return ulCRC;
}

void pushByte(std::uint8_t newChar, std::uint32_t &checksum)
{
    // std::uint32_t oldValue = checksum;
    checksum = aulCrcTable[(checksum ^ newChar) & 0xff] ^ (checksum >> 8);
}

uint32_t computeUtxTime(const int32_t week, const int32_t milliSecsOfWeek, const uint32_t leapSecs, uint32_t &outMillisecs)
{
    constexpr uint32_t MILLIS_IN_SEC = 1000;
    
    /// delta in seconds between:
    ///     - UTC epoch 1970-01-01 00:00:00
    /// and 
    ///     - GPS epoch 1980-01-06 00:00:00
    constexpr uint32_t GPS_EPOCH_TO_UNIX_EPOCH = 3657 * 24 * 3600;

    outMillisecs = milliSecsOfWeek % MILLIS_IN_SEC;

    constexpr uint32_t SECONDS_IN_WEEK = 60 * 60 * 24 * 7;
    uint32_t result = static_cast<uint32_t>(week) * SECONDS_IN_WEEK 
                    + (static_cast<uint32_t>(milliSecsOfWeek) / MILLIS_IN_SEC)
                    - static_cast<uint32_t>(leapSecs)
                    + GPS_EPOCH_TO_UNIX_EPOCH;

    return result;
}

std::string solutionStatusStr(const PppSolutionStatus &pppStatus)
{
    switch (pppStatus)
    {
    case PppSolutionStatus::SOL_COMPUTED:
        return "SOL_COMPUTED";
    case PppSolutionStatus::INSUFFICIENT_OBS:
        return "INSUFFICIENT_OBS";
    case PppSolutionStatus::NO_CONVERGENCE:
        return "NO_CONVERGENCE";
    case PppSolutionStatus::COV_TRACE:
        return "COV_TRACE";
    case PppSolutionStatus::NO_VALUE:
        return "NO_VALUE";
    default:
        return "ERROR";
    }

    return "ERROR";
}

std::string positionTypeStr(const PositionVelocityType &posType)
{
    switch (posType)
    {
    case PositionVelocityType::NONE:
        return "NONE";
    case PositionVelocityType::FIXEDPOS:
        return "FIXEDPOS";
    case PositionVelocityType::FIXEDHEIGHT:
        return "FIXEDHEIGHT";
    case PositionVelocityType::DOPPLER_VELOCITY:
        return "DOPPLER_VELOCITY";
    case PositionVelocityType::SINGLE:
        return "SINGLE";
    case PositionVelocityType::PSRDIFF:
        return "PSRDIFF";
    case PositionVelocityType::SBAS:
        return "SBAS";
    case PositionVelocityType::L1_FLOAT:
        return "L1_FLOAT";
    case PositionVelocityType::IONOFREE_FLOAT:
        return "IONOFREE_FLOAT";
    case PositionVelocityType::NARROW_FLOAT:
        return "NARROW_FLOAT";
    case PositionVelocityType::L1_INT:
        return "L1_INT";
    case PositionVelocityType::WIDE_INT:
        return "WIDE_INT";
    case PositionVelocityType::NARROW_INT:
        return "NARROW_INT";
    case PositionVelocityType::INS:
        return "INS";
    case PositionVelocityType::INS_PSRSP:
        return "INS_PSRSP";
    case PositionVelocityType::INS_PSRDIFF:
        return "INS_PSRDIFF";
    case PositionVelocityType::INS_RTKFLOAT:
        return "INS_RTKFLOAT";
    case PositionVelocityType::INS_RTKFIXED:
        return "INS_RTKFIXED";
    case PositionVelocityType::PPP_CONVERGING:
        return "PPP_CONVERGING";
    case PositionVelocityType::PPP:
        return "PPP";
    case PositionVelocityType::NO_VALUE:
        return "NO_VALUE";
    default:
        return "ERROR";
    }

    return "ERROR";
}
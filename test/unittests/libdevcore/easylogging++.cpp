/**
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 *
 * @brief
 *
 * @file easylogging++.cpp
 * @author: jimmyshi
 * @date 2018-08-28
 */


#include <libdevcore/easylogging++.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <string>
#include <vector>

using namespace el;
using namespace el::base;
using namespace el::base::utils;

static const char* kSysLogLoggerId = "systestlog";

static void reconfigureLoggersForTest(void)
{
    Configurations c;
    c.setGlobally(ConfigurationType::Format, "%datetime{%a %b %d, %H:%m} %msg");
    c.setGlobally(ConfigurationType::Filename, "/tmp/logs/el.gtest.log");
    c.setGlobally(ConfigurationType::MaxLogFileSize, "2097152");  // 2MB
    c.setGlobally(ConfigurationType::ToStandardOutput, "false");
    c.setGlobally(ConfigurationType::PerformanceTracking, "true");
    c.setGlobally(ConfigurationType::LogFlushThreshold, "1");
    Loggers::setDefaultConfigurations(c, true);
    // We do not want to reconfgure syslog with date/time
    Loggers::reconfigureLogger(kSysLogLoggerId, ConfigurationType::Format, "%level: %msg");

    Loggers::addFlag(LoggingFlag::DisableApplicationAbortOnFatalLog);
    Loggers::addFlag(LoggingFlag::ImmediateFlush);
    Loggers::addFlag(LoggingFlag::StrictLogFileSizeCheck);
    Loggers::addFlag(LoggingFlag::DisableVModulesExtensions);
}

namespace dev
{
namespace test
{
static const char* logfile = "/tmp/logs/el.gtest.log";
static const char* kSysLogIdent = "easylogging++ unit test";

class EasyLoggingFixture : TestOutputHelperFixture
{
public:
    /// init test-suite fixture
    EasyLoggingFixture() { reconfigureLoggersForTest(); }
    /// release test-suite fixture
    ~EasyLoggingFixture() {}
};

static std::string tail(unsigned int n, const char* filename = logfile)
{
    if (n == 0)
        return std::string();
    std::ifstream fstr(filename);
    if (!fstr.is_open())
    {
        return std::string();
    }
    fstr.seekg(0, fstr.end);
    int size = static_cast<int>(fstr.tellg());
    int ncopy = n + 1;
    for (int i = (size - 1); i >= 0; --i)
    {
        fstr.seekg(i);
        char c = fstr.get();
        if (c == '\n' && --ncopy == 0)
        {
            break;
        }
        if (i == 0)
        {
            fstr.seekg(i);  // fstr.get() increments buff, so we reset it
        }
    }
    std::stringstream ss;
    char c = fstr.get();
    while (fstr.good())
    {
        ss << c;
        c = fstr.get();
    }
    fstr.close();
    return ss.str();
}

static std::string getDate(const char* format = "%a %b %d, %H:%m")
{
    SubsecondPrecision ssPrec(3);
    return DateTime::getDateTime(format, &ssPrec);
}

#define BUILD_STR(strb)        \
    [&]() -> std::string {     \
        std::stringstream ssb; \
        ssb << strb;           \
        return ssb.str();      \
    }()

BOOST_FIXTURE_TEST_SUITE(easylogging, EasyLoggingFixture)

BOOST_AUTO_TEST_CASE(DateUtilsTest)
{
    auto f = [](unsigned long long v) {
        return DateTime::formatTime(v, base::TimestampUnit::Millisecond);
    };

    BOOST_CHECK_EQUAL("2 ms", f(2));
    BOOST_CHECK_EQUAL("999 ms", f(999));
    BOOST_CHECK_EQUAL("1007 ms", f(1007));
    BOOST_CHECK_EQUAL("1899 ms", f(1899));
    BOOST_CHECK_EQUAL("1 seconds", f(1999));
    BOOST_CHECK_EQUAL("16 minutes", f(999000));
    BOOST_CHECK_EQUAL("24 hours", f(1 * 24 * 60 * 60 * 1000));
    BOOST_CHECK_EQUAL("2 days", f(2 * 24 * 60 * 60 * 1000));
    BOOST_CHECK_EQUAL("7 days", f(7 * 24 * 60 * 60 * 1000));
    BOOST_CHECK_EQUAL("15 days", f(15 * 24 * 60 * 60 * 1000));
}

BOOST_AUTO_TEST_CASE(LevelTestConvertFromString)
{
    BOOST_CHECK(Level::Global == LevelHelper::convertFromString("GLOBAL"));
    BOOST_CHECK(Level::Info == LevelHelper::convertFromString("INFO"));
    BOOST_CHECK(Level::Debug == LevelHelper::convertFromString("DEBUG"));
    BOOST_CHECK(Level::Warning == LevelHelper::convertFromString("WARNING"));
    BOOST_CHECK(Level::Error == LevelHelper::convertFromString("ERROR"));
    BOOST_CHECK(Level::Fatal == LevelHelper::convertFromString("FATAL"));
    BOOST_CHECK(Level::Trace == LevelHelper::convertFromString("TRACE"));
    BOOST_CHECK(Level::Verbose == LevelHelper::convertFromString("VERBOSE"));
    BOOST_CHECK(Level::Unknown == LevelHelper::convertFromString("QA"));
}

BOOST_AUTO_TEST_CASE(LevelTestConvertToString)
{
    BOOST_CHECK_EQUAL("GLOBAL", LevelHelper::convertToString(Level::Global));
    BOOST_CHECK_EQUAL("INFO", LevelHelper::convertToString(Level::Info));
    BOOST_CHECK_EQUAL("DEBUG", LevelHelper::convertToString(Level::Debug));
    BOOST_CHECK_EQUAL("WARNING", LevelHelper::convertToString(Level::Warning));
    BOOST_CHECK_EQUAL("ERROR", LevelHelper::convertToString(Level::Error));
    BOOST_CHECK_EQUAL("FATAL", LevelHelper::convertToString(Level::Fatal));
    BOOST_CHECK_EQUAL("TRACE", LevelHelper::convertToString(Level::Trace));
    BOOST_CHECK_EQUAL("VERBOSE", LevelHelper::convertToString(Level::Verbose));
}

BOOST_AUTO_TEST_CASE(ConfigurationTypeTestConvertFromString)
{
    BOOST_CHECK(
        ConfigurationType::Enabled == ConfigurationTypeHelper::convertFromString("ENABLED"));
    BOOST_CHECK(ConfigurationType::ToFile == ConfigurationTypeHelper::convertFromString("TO_FILE"));
    BOOST_CHECK(ConfigurationType::ToStandardOutput ==
                ConfigurationTypeHelper::convertFromString("TO_STANDARD_OUTPUT"));
    BOOST_CHECK(ConfigurationType::Format == ConfigurationTypeHelper::convertFromString("FORMAT"));
    BOOST_CHECK(
        ConfigurationType::Filename == ConfigurationTypeHelper::convertFromString("FILENAME"));
    BOOST_CHECK(ConfigurationType::SubsecondPrecision ==
                ConfigurationTypeHelper::convertFromString("SUBSECOND_PRECISION"));
    BOOST_CHECK(ConfigurationType::PerformanceTracking ==
                ConfigurationTypeHelper::convertFromString("PERFORMANCE_TRACKING"));
    BOOST_CHECK(ConfigurationType::MaxLogFileSize ==
                ConfigurationTypeHelper::convertFromString("MAX_LOG_FILE_SIZE"));
    BOOST_CHECK(ConfigurationType::LogFlushThreshold ==
                ConfigurationTypeHelper::convertFromString("LOG_FLUSH_THRESHOLD"));
}

BOOST_AUTO_TEST_CASE(ConfigurationTypeTestConvertToString)
{
    BOOST_CHECK_EQUAL(
        "ENABLED", ConfigurationTypeHelper::convertToString(ConfigurationType::Enabled));
    BOOST_CHECK_EQUAL(
        "TO_FILE", ConfigurationTypeHelper::convertToString(ConfigurationType::ToFile));
    BOOST_CHECK_EQUAL("TO_STANDARD_OUTPUT",
        ConfigurationTypeHelper::convertToString(ConfigurationType::ToStandardOutput));
    BOOST_CHECK_EQUAL(
        "FORMAT", ConfigurationTypeHelper::convertToString(ConfigurationType::Format));
    BOOST_CHECK_EQUAL(
        "FILENAME", ConfigurationTypeHelper::convertToString(ConfigurationType::Filename));
    BOOST_CHECK_EQUAL("SUBSECOND_PRECISION",
        ConfigurationTypeHelper::convertToString(ConfigurationType::SubsecondPrecision));
    BOOST_CHECK_EQUAL("PERFORMANCE_TRACKING",
        ConfigurationTypeHelper::convertToString(ConfigurationType::PerformanceTracking));
    BOOST_CHECK_EQUAL("MAX_LOG_FILE_SIZE",
        ConfigurationTypeHelper::convertToString(ConfigurationType::MaxLogFileSize));
    BOOST_CHECK_EQUAL("LOG_FLUSH_THRESHOLD",
        ConfigurationTypeHelper::convertToString(ConfigurationType::LogFlushThreshold));
}

#define TEST_LEVEL(l, name)                                                                      \
    BOOST_AUTO_TEST_CASE(WriteAllTest##l)                                                        \
    {                                                                                            \
        std::string s;                                                                           \
        LOG(l) << name << " 1";                                                                  \
        s = BUILD_STR(getDate() << " " << name << " 1\n");                                       \
        BOOST_CHECK(s == tail(1));                                                               \
        LOG_IF(true, l) << name << " 2";                                                         \
        s = BUILD_STR(getDate() << " " << name << " 1\n" << getDate() << " " << name << " 2\n"); \
        BOOST_CHECK(s == tail(2));                                                               \
        LOG_IF(true, l) << name << " 3";                                                         \
        s = BUILD_STR(getDate() << " " << name << " 3\n");                                       \
        BOOST_CHECK(s == tail(1));                                                               \
        LOG_IF(false, l) << "SHOULD NOT LOG";                                                    \
        s = BUILD_STR(getDate() << " " << name << " 3\n");                                       \
        BOOST_CHECK(s == tail(1));                                                               \
        LOG_EVERY_N(1, l) << name << " every n=1";                                               \
        s = BUILD_STR(getDate() << " " << name << " every n=1\n");                               \
        LOG_AFTER_N(1, l) << name << " after n=1";                                               \
        s = BUILD_STR(getDate() << " " << name << " after n=1\n");                               \
        LOG_N_TIMES(2, l) << name << " n times=2";                                               \
        s = BUILD_STR(getDate() << " " << name << " n times=2\n");                               \
        BOOST_CHECK(s == tail(1));                                                               \
    }

TEST_LEVEL(DEBUG, "Debug")
TEST_LEVEL(INFO, "Info")
TEST_LEVEL(ERROR, "Error")
TEST_LEVEL(WARNING, "Warning")
TEST_LEVEL(FATAL, "Fatal")
TEST_LEVEL(TRACE, "Trace")

/*
BOOST_AUTO_TEST_CASE(WriteAllTestVERBOSE)
{
    Configurations cOld(*Loggers::getLogger("default")->configurations());
    Loggers::reconfigureAllLoggers(
        ConfigurationType::Format, "%datetime{%a %b %d, %H:%m} %level-%vlevel %msg");

    el::Loggers::addFlag(el::LoggingFlag::AllowVerboseIfModuleNotSpecified);  // Accept all verbose
                                                                              // levels; we already
                                                                              // have vmodules!

    std::string s;
    for (int i = 1; i <= 6; ++i)
        VLOG_EVERY_N(2, 2) << "every n=" << i;

    s = BUILD_STR(getDate() << " VERBOSE-2 every n=2\n"
                            << getDate() << " VERBOSE-2 every n=4\n"
                            << getDate() << " VERBOSE-2 every n=6\n");
    BOOST_CHECK_EQUAL(s, tail(3));

    VLOG_IF(true, 3) << "Test conditional verbose log";
    s = BUILD_STR(getDate() << " VERBOSE-3 Test conditional verbose log\n");
    BOOST_CHECK_EQUAL(s, tail(1));

    VLOG_IF(false, 3) << "SHOULD NOT LOG";
    // Should not log!
    BOOST_CHECK_EQUAL(s, tail(1));

    VLOG(3) << "Log normally (verbose)";
    s = BUILD_STR(getDate() << " VERBOSE-3 Log normally (verbose)\n");
    BOOST_CHECK_EQUAL(s, tail(1));

    // Reset it back to old
    Loggers::reconfigureAllLoggers(cOld);
}
*/

BOOST_AUTO_TEST_CASE(WriteAllTestEVERY_N)
{
    std::string s;
    const char* levelName = "INFO";
    for (int i = 1; i <= 6; ++i)
        LOG_EVERY_N(2, INFO) << levelName << " every n=" << i;

    s = BUILD_STR(getDate() << " " << levelName << " every n=2\n"
                            << getDate() << " " << levelName << " every n=4\n"
                            << getDate() << " " << levelName << " every n=6\n");
    BOOST_CHECK_EQUAL(s, tail(3));
}

BOOST_AUTO_TEST_CASE(WriteAllTestAFTER_N)
{
    std::string s;
    const char* levelName = "INFO";
    for (int i = 1; i <= 6; ++i)
        LOG_AFTER_N(2, INFO) << levelName << " after n=" << i;

    s = BUILD_STR(getDate() << " " << levelName << " after n=3\n"
                            << getDate() << " " << levelName << " after n=4\n"
                            << getDate() << " " << levelName << " after n=5\n"
                            << getDate() << " " << levelName << " after n=6\n");
    BOOST_CHECK_EQUAL(s, tail(4));
}

BOOST_AUTO_TEST_CASE(WriteAllTestN_TIMES)
{
    std::string s;
    const char* levelName = "INFO";
    for (int i = 1; i <= 6; ++i)
        LOG_N_TIMES(4, INFO) << levelName << " n times=" << i;

    s = BUILD_STR(getDate() << " " << levelName << " n times=1\n"
                            << getDate() << " " << levelName << " n times=2\n"
                            << getDate() << " " << levelName << " n times=3\n"
                            << getDate() << " " << levelName << " n times=4\n");
    BOOST_CHECK_EQUAL(s, tail(4));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
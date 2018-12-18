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

static bool fileExists(const char* filename)
{
    el::base::type::fstream_t fstr(filename, el::base::type::fstream_t::in);
    return fstr.is_open();
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

BOOST_AUTO_TEST_CASE(ConfigurationsTestSet)
{
    Configurations c;
    c.set(Level::Info, el::ConfigurationType::Enabled, "true");
    c.set(Level::Info, el::ConfigurationType::Enabled, "true");
    BOOST_CHECK_EQUAL(c.size(), 1);
    Configurations c2;
    c2 = c;
    c2.set(Level::Info, el::ConfigurationType::Enabled, "false");
    BOOST_CHECK_EQUAL(c.get(Level::Info, el::ConfigurationType::Enabled)->value(), "true");
    BOOST_CHECK_EQUAL(c2.get(Level::Info, el::ConfigurationType::Enabled)->value(), "false");
    BOOST_CHECK_EQUAL(c2.size(), 1);
}

BOOST_AUTO_TEST_CASE(ConfigurationsTestHasConfiguration)
{
    Configurations c;
    c.set(Level::Info, el::ConfigurationType::Enabled, "true");
    c.set(Level::Debug, el::ConfigurationType::Enabled, "false");
    c.set(Level::Info, el::ConfigurationType::Format, "%level: %msg");

    BOOST_CHECK(c.hasConfiguration(ConfigurationType::Enabled));
    BOOST_CHECK(c.hasConfiguration(ConfigurationType::Format));
    BOOST_CHECK(!c.hasConfiguration(ConfigurationType::Filename));
    BOOST_CHECK(!c.hasConfiguration(ConfigurationType::MaxLogFileSize));

    BOOST_CHECK(c.hasConfiguration(Level::Debug, ConfigurationType::Enabled));
    BOOST_CHECK(!c.hasConfiguration(Level::Verbose, ConfigurationType::Format));
}

BOOST_AUTO_TEST_CASE(ConfigurationsTestSetForAllLevels)
{
    Configurations c;
    c.setGlobally(el::ConfigurationType::Enabled, "true");
    BOOST_CHECK(!c.hasConfiguration(Level::Global, ConfigurationType::Enabled));
    BOOST_CHECK(c.hasConfiguration(Level::Debug, ConfigurationType::Enabled));
    BOOST_CHECK(c.hasConfiguration(Level::Info, ConfigurationType::Enabled));
    BOOST_CHECK(c.hasConfiguration(Level::Warning, ConfigurationType::Enabled));
    BOOST_CHECK(c.hasConfiguration(Level::Error, ConfigurationType::Enabled));
    BOOST_CHECK(c.hasConfiguration(Level::Fatal, ConfigurationType::Enabled));
    BOOST_CHECK(c.hasConfiguration(Level::Verbose, ConfigurationType::Enabled));
    BOOST_CHECK(c.hasConfiguration(Level::Trace, ConfigurationType::Enabled));
}


BOOST_AUTO_TEST_CASE(ConfigurationsTestParsingFromFile)
{
    std::fstream confFile("/tmp/temp-test.conf", std::fstream::out);
    confFile << " * GLOBAL:\n"
             << "    FORMAT               =  %datetime %level %msg\n"
             << "* INFO:\n"
             // Following should be included in format because its inside the quotes
             << "    FORMAT               =  \"%datetime %level [%user@%host] [%func] [%loc] "
                "%msg## This should be included in format\" ## This should be excluded\n"
             << "* DEBUG:\n"
             << "    FORMAT               =  %datetime %level [%user@%host] [%func] [%loc] %msg ## "
                "Comment before EOL char\n"
             << "## Comment on empty line\n"
             // WARNING is defined by GLOBAL
             << "* ERROR:\n"
             << "    FORMAT               =  %datetime %level %msg\n"
             << "* FATAL:\n"
             << "    FORMAT               =  %datetime %level %msg\n"
             << "* VERBOSE:\n"
             << "    FORMAT               =  %datetime %level-%vlevel %msg\n"
             << "* TRACE:\n"
             << "    FORMAT               =  %datetime %level [%func] [%loc] %msg\n";
    confFile.close();

    Configurations c("/tmp/temp-test.conf", false, nullptr);
    BOOST_CHECK(!c.hasConfiguration(Level::Debug, ConfigurationType::Enabled));
    BOOST_CHECK(!c.hasConfiguration(Level::Global, ConfigurationType::Enabled));
    BOOST_CHECK(c.hasConfiguration(Level::Global, ConfigurationType::Format));
    BOOST_CHECK(c.hasConfiguration(Level::Info, ConfigurationType::Format));
    BOOST_CHECK_EQUAL(
        "%datetime %level %msg", c.get(Level::Global, ConfigurationType::Format)->value());
    BOOST_CHECK_EQUAL(
        "%datetime %level [%user@%host] [%func] [%loc] %msg## This should be included in format",
        c.get(Level::Info, ConfigurationType::Format)->value());
    BOOST_CHECK_EQUAL("%datetime %level [%user@%host] [%func] [%loc] %msg",
        c.get(Level::Debug, ConfigurationType::Format)->value());
    BOOST_CHECK_EQUAL(
        "%datetime %level %msg", c.get(Level::Warning, ConfigurationType::Format)->value());
    BOOST_CHECK_EQUAL(
        "%datetime %level %msg", c.get(Level::Error, ConfigurationType::Format)->value());
    BOOST_CHECK_EQUAL(
        "%datetime %level %msg", c.get(Level::Fatal, ConfigurationType::Format)->value());
    BOOST_CHECK_EQUAL(
        "%datetime %level-%vlevel %msg", c.get(Level::Verbose, ConfigurationType::Format)->value());
    BOOST_CHECK_EQUAL("%datetime %level [%func] [%loc] %msg",
        c.get(Level::Trace, ConfigurationType::Format)->value());
}

BOOST_AUTO_TEST_CASE(ConfigurationsTestParsingFromText)
{
    std::stringstream ss;
    ss << " * GLOBAL:\n"
       << "    FORMAT               =  %datetime{%d/%M/%Y %h:%m:%s,%g} %level %msg\n"
       << "* DEBUG:\n"
       << "    FORMAT               =  %datetime %level [%user@%host] [%func] [%loc] %msg\n"
       // INFO and WARNING uses is defined by GLOBAL
       << "* ERROR:\n"
       << "    FORMAT               =  %datetime %level %msg\n"
       << "* FATAL:\n"
       << "    FORMAT               =  %datetime %level %msg\n"
       << "* VERBOSE:\n"
       << "    FORMAT               =  %datetime %level-%vlevel %msg\n"
       << "* TRACE:\n"
       << "    FORMAT               =  %datetime %level [%func] [%loc] %msg\n";

    Configurations c;
    c.parseFromText(ss.str());
    BOOST_CHECK(!c.hasConfiguration(Level::Debug, ConfigurationType::Enabled));
    BOOST_CHECK(!c.hasConfiguration(Level::Global, ConfigurationType::Enabled));
    BOOST_CHECK(c.hasConfiguration(Level::Global, ConfigurationType::Format));
    BOOST_CHECK(c.hasConfiguration(Level::Info, ConfigurationType::Format));
    BOOST_CHECK_EQUAL("%datetime{%d/%M/%Y %h:%m:%s,%g} %level %msg",
        c.get(Level::Global, ConfigurationType::Format)->value());
    BOOST_CHECK_EQUAL("%datetime{%d/%M/%Y %h:%m:%s,%g} %level %msg",
        c.get(Level::Info, ConfigurationType::Format)->value());
    BOOST_CHECK_EQUAL("%datetime{%d/%M/%Y %h:%m:%s,%g} %level %msg",
        c.get(Level::Warning, ConfigurationType::Format)->value());
    BOOST_CHECK_EQUAL("%datetime %level [%user@%host] [%func] [%loc] %msg",
        c.get(Level::Debug, ConfigurationType::Format)->value());
    BOOST_CHECK_EQUAL(
        "%datetime %level %msg", c.get(Level::Error, ConfigurationType::Format)->value());
    BOOST_CHECK_EQUAL(
        "%datetime %level %msg", c.get(Level::Fatal, ConfigurationType::Format)->value());
    BOOST_CHECK_EQUAL(
        "%datetime %level-%vlevel %msg", c.get(Level::Verbose, ConfigurationType::Format)->value());
    BOOST_CHECK_EQUAL("%datetime %level [%func] [%loc] %msg",
        c.get(Level::Trace, ConfigurationType::Format)->value());
}

BOOST_AUTO_TEST_CASE(ConfigurationsTestParsingFromTextWithEscape)
{
    std::stringstream ss;
    ss << " * GLOBAL:\n"
       << "    FORMAT               =  %datetime{%d/%M/%Y %h:%m:%s,%g} %level %msg\n"
       << "* DEBUG:\n"
       << "    FORMAT               =  \"%datetime %level [%user@%host] [%func] [%loc] \\\"inside "
          "quotes\\\" %msg\"\n"
       // INFO and WARNING uses is defined by GLOBAL
       << "* ERROR:\n"
       << "    FORMAT               =  \"%datetime %level \\\"##hash##\\\" %msg\"\n"
       << "* FATAL:\n"
       << "    FORMAT               =  %datetime %level ## Comment out log format specifier "
          "temporarily %msg\n"
       << "* VERBOSE:\n"
       << "    FORMAT               =  %datetime %level-%vlevel %msg\n"
       << "* TRACE:\n"
       << "    FORMAT               =  %datetime %level [%func] [%loc] %msg\n";

    Configurations c;
    c.parseFromText(ss.str());
    BOOST_CHECK_EQUAL("%datetime{%d/%M/%Y %h:%m:%s,%g} %level %msg",
        c.get(Level::Global, ConfigurationType::Format)->value());
    BOOST_CHECK_EQUAL("%datetime{%d/%M/%Y %h:%m:%s,%g} %level %msg",
        c.get(Level::Info, ConfigurationType::Format)->value());
    BOOST_CHECK_EQUAL("%datetime{%d/%M/%Y %h:%m:%s,%g} %level %msg",
        c.get(Level::Warning, ConfigurationType::Format)->value());
    BOOST_CHECK_EQUAL("%datetime %level [%user@%host] [%func] [%loc] \"inside quotes\" %msg",
        c.get(Level::Debug, ConfigurationType::Format)->value());
    BOOST_CHECK_EQUAL("%datetime %level \"##hash##\" %msg",
        c.get(Level::Error, ConfigurationType::Format)->value());
    BOOST_CHECK_EQUAL("%datetime %level", c.get(Level::Fatal, ConfigurationType::Format)->value());
    BOOST_CHECK_EQUAL(
        "%datetime %level-%vlevel %msg", c.get(Level::Verbose, ConfigurationType::Format)->value());
    BOOST_CHECK_EQUAL("%datetime %level [%func] [%loc] %msg",
        c.get(Level::Trace, ConfigurationType::Format)->value());
}

BOOST_AUTO_TEST_CASE(CommandLineArgsTestSetArgs)
{
    const char* c[10];
    c[0] = "myprog";
    c[1] = "-arg1";
    c[2] = "--arg2WithValue=1";
    c[3] = "--arg3WithValue=something=some_other_thing";
    c[4] = "-arg1";  // Shouldn't be added
    c[5] = "--arg3WithValue=this_should_be_ignored_since_key_already_exist";
    c[6] = "--arg4WithValue=this_should_Added";
    c[7] = "\0";
    CommandLineArgs cmd(7, c);
    BOOST_CHECK_EQUAL(4, cmd.size());
    BOOST_CHECK(!cmd.hasParamWithValue("-arg1"));
    BOOST_CHECK(!cmd.hasParam("--arg2WithValue"));
    BOOST_CHECK(!cmd.hasParam("--arg3WithValue"));
    BOOST_CHECK(cmd.hasParamWithValue("--arg2WithValue"));
    BOOST_CHECK(cmd.hasParamWithValue("--arg3WithValue"));
    BOOST_CHECK(cmd.hasParam("-arg1"));
    BOOST_CHECK_EQUAL(cmd.getParamValue("--arg2WithValue"), "1");
    BOOST_CHECK_EQUAL(cmd.getParamValue("--arg3WithValue"), "something=some_other_thing");
    BOOST_CHECK_EQUAL(cmd.getParamValue("--arg4WithValue"), "this_should_Added");
}


BOOST_AUTO_TEST_CASE(CommandLineArgsTestLoggingFlagsArg)
{
    const char* c[3];
    c[0] = "myprog";
    c[1] = "--logging-flags=5";  // NewLineForContainer & LogDetailedCrashReason (1 & 4)
    c[2] = "\0";

    unsigned short currFlags = ELPP->flags();  // For resetting after test

    BOOST_CHECK(!Loggers::hasFlag(LoggingFlag::NewLineForContainer));
    BOOST_CHECK(!Loggers::hasFlag(LoggingFlag::LogDetailedCrashReason));

    Helpers::setArgs(2, c);

    // BOOST_CHECK(Loggers::hasFlag(LoggingFlag::NewLineForContainer));
    // BOOST_CHECK(Loggers::hasFlag(LoggingFlag::LogDetailedCrashReason));

    // Reset to original state
    std::stringstream resetter;
    resetter << "--logging-flags=" << currFlags;
    c[1] = resetter.str().c_str();
    Helpers::setArgs(2, c);
    BOOST_CHECK(!Loggers::hasFlag(LoggingFlag::NewLineForContainer));
    BOOST_CHECK(!Loggers::hasFlag(LoggingFlag::LogDetailedCrashReason));
}


const char* getIp(const el::LogMessage*)
{
    return "127.0.0.1";
}

BOOST_AUTO_TEST_CASE(CustomFormatSpecifierTestTestInstall)
{
    BOOST_CHECK(!el::Helpers::hasCustomFormatSpecifier("%ip"));
    el::Helpers::installCustomFormatSpecifier(el::CustomFormatSpecifier("%ip", getIp));
    BOOST_CHECK(el::Helpers::hasCustomFormatSpecifier("%ip"));
}

BOOST_AUTO_TEST_CASE(CustomFormatSpecifierTestTestResolution)
{
    Configurations c;
    c.setGlobally(el::ConfigurationType::Format, "%datetime{%a %b %d, %H:%m} %ip: %msg");
    el::Loggers::reconfigureLogger(consts::kDefaultLoggerId, c);
    el::Helpers::installCustomFormatSpecifier(el::CustomFormatSpecifier("%ip", getIp));
    LOG(INFO) << "My ip test";
    std::string s = BUILD_STR(getDate() << " 127.0.0.1: My ip test\n");
    BOOST_CHECK_EQUAL(s, tail(1));
    // Reset back
    reconfigureLoggersForTest();
}

BOOST_AUTO_TEST_CASE(CustomFormatSpecifierTestTestUnInstall)
{
    el::Helpers::installCustomFormatSpecifier(el::CustomFormatSpecifier("%ip", getIp));
    BOOST_CHECK(el::Helpers::hasCustomFormatSpecifier("%ip"));
    el::Helpers::uninstallCustomFormatSpecifier("%ip");
    BOOST_CHECK(!el::Helpers::hasCustomFormatSpecifier("%ip"));
}


static const char* filename = "/tmp/files_utils_test";
static el::base::type::fstream_t* fs;


static void cleanFile(const char* filename = logfile, el::base::type::fstream_t* fs = nullptr)
{
    if (fs != nullptr && fs->is_open())
    {
        fs->close();
        fs->open(filename, el::base::type::fstream_t::out);
    }
    else
    {
        el::base::type::fstream_t f(filename, el::base::type::fstream_t::out);
        if (f.is_open())
        {
            f.close();
        }
        ELPP_UNUSED(f);
    }
}

static void removeFile(const char* path)
{
    (void)(system(BUILD_STR("rm -rf " << path).c_str()) + 1);  // (void)(...+1) -> ignore result for
                                                               // gcc 4.6+
}

BOOST_AUTO_TEST_CASE(FileUtilsTestNewFileStream)
{
    fs = File::newFileStream(filename);
    // BOOST_CHECK_NE(NULL, fs);
    BOOST_CHECK(fs->is_open());
    cleanFile(filename, fs);
}


BOOST_AUTO_TEST_CASE(FileUtilsTestGetSizeOfFile)
{
    fs = File::newFileStream(filename);
    BOOST_CHECK_EQUAL(File::getSizeOfFile(fs), 0);
    const char* data = "123";
    (*fs) << data;
    fs->flush();
    BOOST_CHECK_EQUAL(File::getSizeOfFile(fs), strlen(data));
}

BOOST_AUTO_TEST_CASE(FileUtilsTestPathExists)
{
    BOOST_CHECK(File::pathExists(filename));
    removeFile(filename);
    BOOST_CHECK(!File::pathExists(filename));
}

BOOST_AUTO_TEST_CASE(FileUtilsTestExtractPathFromFilename)
{
    BOOST_CHECK_EQUAL(
        "/this/is/path/on/unix/", File::extractPathFromFilename("/this/is/path/on/unix/file.txt"));
    BOOST_CHECK_EQUAL("C:\\this\\is\\path\\on\\win\\",
        File::extractPathFromFilename("C:\\this\\is\\path\\on\\win\\file.txt", "\\"));
}

BOOST_AUTO_TEST_CASE(FileUtilsTestCreatePath)
{
    const char* path = "/tmp/my/one/long/path";
    BOOST_CHECK(!File::pathExists(path));
    BOOST_CHECK(File::createPath(path));
    BOOST_CHECK(File::pathExists(path));
    removeFile(path);
    BOOST_CHECK(!File::pathExists(path));
}

BOOST_AUTO_TEST_CASE(FileUtilsTestBuildStrippedFilename)
{
    char buf[50] = "";

    File::buildStrippedFilename("this_is_myfile.cc", buf, 50);
    BOOST_CHECK_EQUAL("this_is_myfile.cc", buf);

    Str::clearBuff(buf, 20);
    BOOST_CHECK_EQUAL("", buf);

    File::buildStrippedFilename("this_is_myfilename_with_more_than_50_characters.cc", buf, 50);
    BOOST_CHECK_EQUAL("..s_is_myfilename_with_more_than_50_characters.cc", buf);
}

BOOST_AUTO_TEST_CASE(FormatSpecifierTestTestFBaseSpecifier)
{
    Configurations c;
    c.setGlobally(el::ConfigurationType::Format, "%datetime{%a %b %d, %H:%m} %fbase: %msg");
    el::Loggers::reconfigureLogger(consts::kDefaultLoggerId, c);
    LOG(INFO) << "My fbase test";
    std::string s = BUILD_STR(getDate() << " easylogging++.cpp: My fbase test\n");
    BOOST_CHECK_EQUAL(s, tail(1));
    // Reset back
    reconfigureLoggersForTest();
}

BOOST_AUTO_TEST_CASE(FormatSpecifierTestTestLevShortSpecifier)
{
    const char* param[10];
    param[0] = "myprog";
    param[1] = "--v=5";
    param[2] = "\0";
    el::Helpers::setArgs(2, param);


    // Regression origional %level still correct
    Configurations c;
    c.setGlobally(el::ConfigurationType::Format, "%level %msg");
    c.set(el::Level::Verbose, el::ConfigurationType::Format, "%level-%vlevel %msg");
    el::Loggers::reconfigureLogger(consts::kDefaultLoggerId, c);
    {
        std::string levelINFO = "INFO hello world\n";
        std::string levelDEBUG = "DEBUG hello world\n";
        std::string levelWARN = "WARNING hello world\n";
        std::string levelERROR = "ERROR hello world\n";
        std::string levelFATAL = "FATAL hello world\n";
        std::string levelVER = "VERBOSE-2 hello world\n";
        std::string levelTRACE = "TRACE hello world\n";
        LOG(INFO) << "hello world";
        BOOST_CHECK_EQUAL(levelINFO, tail(1));
        LOG(DEBUG) << "hello world";
        BOOST_CHECK_EQUAL(levelDEBUG, tail(1));
        LOG(WARNING) << "hello world";
        BOOST_CHECK_EQUAL(levelWARN, tail(1));
        LOG(ERROR) << "hello world";
        BOOST_CHECK_EQUAL(levelERROR, tail(1));
        LOG(FATAL) << "hello world";
        BOOST_CHECK_EQUAL(levelFATAL, tail(1));
        VLOG(2) << "hello world";
        BOOST_CHECK_EQUAL(levelVER, tail(1));
        LOG(TRACE) << "hello world";
        BOOST_CHECK_EQUAL(levelTRACE, tail(1));
    }

    // Test %levshort new specifier
    c.setGlobally(el::ConfigurationType::Format, "%levshort  %msg");
    c.set(el::Level::Verbose, el::ConfigurationType::Format, "%levshort%vlevel %msg");
    el::Loggers::reconfigureLogger(consts::kDefaultLoggerId, c);
    {
        std::string levelINFO = "I  hello world\n";
        std::string levelDEBUG = "D  hello world\n";
        std::string levelWARN = "W  hello world\n";
        std::string levelERROR = "E  hello world\n";
        std::string levelFATAL = "F  hello world\n";
        std::string levelVER = "V2 hello world\n";
        std::string levelTRACE = "T  hello world\n";
        LOG(INFO) << "hello world";
        BOOST_CHECK_EQUAL(levelINFO, tail(1));
        LOG(DEBUG) << "hello world";
        BOOST_CHECK_EQUAL(levelDEBUG, tail(1));
        LOG(WARNING) << "hello world";
        BOOST_CHECK_EQUAL(levelWARN, tail(1));
        LOG(ERROR) << "hello world";
        BOOST_CHECK_EQUAL(levelERROR, tail(1));
        LOG(FATAL) << "hello world";
        BOOST_CHECK_EQUAL(levelFATAL, tail(1));
        VLOG(2) << "hello world";
        BOOST_CHECK_EQUAL(levelVER, tail(1));
        LOG(TRACE) << "hello world";
        BOOST_CHECK_EQUAL(levelTRACE, tail(1));
    }

    // Reset back
    reconfigureLoggersForTest();
}

BOOST_AUTO_TEST_CASE(GlobalConfigurationTestParse)
{
    const char* globalConfFile = "/tmp/global-conf-test.conf";
    std::fstream confFile(globalConfFile, std::fstream::out);
    confFile << "-- performance\n"
             << "    ## This just skips configuring performance logger any more.\n"
             << "-- global-test-logger\n"
             << "* GLOBAL:\n"
             << "    FORMAT               =  GLOBAL_TEST\n"
             << "* INFO:\n"
             // Following should be included in format because its inside the quotes
             << "* DEBUG:\n"
             << "    FORMAT               =  %datetime %level [%user@%host] [%func] [%loc] %msg ## "
                "Comment before EOL char\n"
             << "## Comment on empty line\n"
             // WARNING is defined by GLOBAL
             << "* ERROR:\n"
             << "    FORMAT               =  %datetime %level %msg\n"
             << "* FATAL:\n"
             << "    FORMAT               =  %datetime %level %msg\n"
             << "* VERBOSE:\n"
             << "    FORMAT               =  %datetime %level-%vlevel %msg\n"
             << "* TRACE:\n"
             << "    FORMAT               =  %datetime %level [%func] [%loc] %msg\n";
    confFile.close();

    Logger* perfLogger = Loggers::getLogger("performance", false);
    BOOST_CHECK(perfLogger != NULL);

    std::string perfFormatConf =
        perfLogger->configurations()->get(Level::Info, ConfigurationType::Format)->value();
    std::string perfFilenameConf =
        perfLogger->configurations()->get(Level::Info, ConfigurationType::Filename)->value();
    std::size_t totalLoggers = elStorage->registeredLoggers()->size();

    BOOST_CHECK(Loggers::getLogger("global-test-logger", false) == nullptr);

    Loggers::configureFromGlobal(globalConfFile);

    BOOST_CHECK_EQUAL(totalLoggers + 1, elStorage->registeredLoggers()->size());
    BOOST_CHECK_EQUAL(perfFormatConf,
        perfLogger->configurations()->get(Level::Info, ConfigurationType::Format)->value());
    BOOST_CHECK_EQUAL(perfFilenameConf,
        perfLogger->configurations()->get(Level::Info, ConfigurationType::Filename)->value());

    // Not nullptr anymore
    Logger* testLogger = Loggers::getLogger("global-test-logger", false);
    BOOST_CHECK(testLogger != nullptr);

    BOOST_CHECK_EQUAL("GLOBAL_TEST",
        testLogger->configurations()->get(Level::Info, ConfigurationType::Format)->value());
    BOOST_CHECK_EQUAL("%datetime %level [%user@%host] [%func] [%loc] %msg",
        testLogger->configurations()->get(Level::Debug, ConfigurationType::Format)->value());
}

BOOST_AUTO_TEST_CASE(RegisteredHitCountersTestValidationEveryN)
{
    RegisteredHitCounters r;

    // Ensure no hit counters are registered yet
    BOOST_CHECK(r.empty());

    unsigned long int line = __LINE__;
    r.validateEveryN(__FILE__, line, 2);

    // Confirm size
    BOOST_CHECK_EQUAL(1, r.size());

    // Confirm hit count
    BOOST_CHECK_EQUAL(1, r.getCounter(__FILE__, line)->hitCounts());

    // Confirm validations
    BOOST_CHECK(r.validateEveryN(__FILE__, line, 2));
    BOOST_CHECK(!r.validateEveryN(__FILE__, line, 2));

    BOOST_CHECK(r.validateEveryN(__FILE__, line, 2));
    BOOST_CHECK(!r.validateEveryN(__FILE__, line, 2));

    BOOST_CHECK(r.validateEveryN(__FILE__, line, 2));
    BOOST_CHECK(!r.validateEveryN(__FILE__, line, 2));

    line = __LINE__;
    r.validateEveryN(__FILE__, line, 3);
    // Confirm size
    BOOST_CHECK_EQUAL(2, r.size());
    // Confirm hitcounts
    BOOST_CHECK_EQUAL(1, r.getCounter(__FILE__, line)->hitCounts());

    // Confirm validations
    BOOST_CHECK(!r.validateEveryN(__FILE__, line, 3));
    BOOST_CHECK(r.validateEveryN(__FILE__, line, 3));
    BOOST_CHECK(!r.validateEveryN(__FILE__, line, 3));

    BOOST_CHECK(!r.validateEveryN(__FILE__, line, 3));
    BOOST_CHECK(r.validateEveryN(__FILE__, line, 3));
    BOOST_CHECK(!r.validateEveryN(__FILE__, line, 3));

    BOOST_CHECK(!r.validateEveryN(__FILE__, line, 3));
    BOOST_CHECK(r.validateEveryN(__FILE__, line, 3));
    BOOST_CHECK(!r.validateEveryN(__FILE__, line, 3));

    BOOST_CHECK(!r.validateEveryN(__FILE__, line, 3));
    BOOST_CHECK(r.validateEveryN(__FILE__, line, 3));
    BOOST_CHECK(!r.validateEveryN(__FILE__, line, 3));

    BOOST_CHECK(!r.validateEveryN(__FILE__, line, 3));
    BOOST_CHECK(r.validateEveryN(__FILE__, line, 3));
    BOOST_CHECK(!r.validateEveryN(__FILE__, line, 3));

    // Confirm size once again
    BOOST_CHECK_EQUAL(2, r.size());
}


BOOST_AUTO_TEST_CASE(RegisteredHitCountersTestValidationAfterN)
{
    RegisteredHitCounters r;

    // Ensure no hit counters are registered yet
    BOOST_CHECK(r.empty());

    unsigned long int line = __LINE__;
    unsigned int n = 2;

    // Register
    r.validateAfterN(__FILE__, line, n);  // 1

    // Confirm size
    BOOST_CHECK_EQUAL(1, r.size());

    // Confirm hit count
    BOOST_CHECK_EQUAL(1, r.getCounter(__FILE__, line)->hitCounts());

    // Confirm validations
    BOOST_CHECK(!r.validateAfterN(__FILE__, line, n));  // 2
    BOOST_CHECK(r.validateAfterN(__FILE__, line, n));   // 3
    BOOST_CHECK(r.validateAfterN(__FILE__, line, n));   // 4
    BOOST_CHECK(r.validateAfterN(__FILE__, line, n));   // 5
    BOOST_CHECK(r.validateAfterN(__FILE__, line, n));   // 6
}

BOOST_AUTO_TEST_CASE(RegisteredHitCountersTestValidationNTimes)
{
    RegisteredHitCounters r;

    // Ensure no hit counters are registered yet
    BOOST_CHECK(r.empty());

    unsigned long int line = __LINE__;
    unsigned int n = 5;

    // Register
    r.validateNTimes(__FILE__, line, n);  // 1

    // Confirm size
    BOOST_CHECK_EQUAL(1, r.size());

    // Confirm hit count
    BOOST_CHECK_EQUAL(1, r.getCounter(__FILE__, line)->hitCounts());

    // Confirm validations
    BOOST_CHECK(r.validateNTimes(__FILE__, line, n));   // 2
    BOOST_CHECK(r.validateNTimes(__FILE__, line, n));   // 3
    BOOST_CHECK(r.validateNTimes(__FILE__, line, n));   // 4
    BOOST_CHECK(r.validateNTimes(__FILE__, line, n));   // 5
    BOOST_CHECK(!r.validateNTimes(__FILE__, line, n));  // 6
    BOOST_CHECK(!r.validateNTimes(__FILE__, line, n));  // 7
    BOOST_CHECK(!r.validateNTimes(__FILE__, line, n));  // 8
    BOOST_CHECK(!r.validateNTimes(__FILE__, line, n));  // 9
}

BOOST_AUTO_TEST_CASE(LogFormatResolutionTestNormalFormat)
{
    LogFormat format(Level::Info, ELPP_LITERAL("%logger %thread"));
    BOOST_CHECK_EQUAL(ELPP_LITERAL("%logger %thread"), format.userFormat());
    BOOST_CHECK_EQUAL(ELPP_LITERAL("%logger %thread"), format.format());
    BOOST_CHECK_EQUAL("", format.dateTimeFormat());

    LogFormat format2(Level::Info, ELPP_LITERAL("%logger %datetime{%Y-%M-%d %h:%m:%s  } %thread"));
    BOOST_CHECK_EQUAL(
        ELPP_LITERAL("%logger %datetime{%Y-%M-%d %h:%m:%s  } %thread"), format2.userFormat());
    BOOST_CHECK_EQUAL(ELPP_LITERAL("%logger %datetime %thread"), format2.format());
    BOOST_CHECK_EQUAL("%Y-%M-%d %h:%m:%s  ", format2.dateTimeFormat());

    LogFormat format3(Level::Info, ELPP_LITERAL("%logger %datetime{%Y-%M-%d} %thread"));
    BOOST_CHECK_EQUAL(ELPP_LITERAL("%logger %datetime{%Y-%M-%d} %thread"), format3.userFormat());
    BOOST_CHECK_EQUAL(ELPP_LITERAL("%logger %datetime %thread"), format3.format());
    BOOST_CHECK_EQUAL("%Y-%M-%d", format3.dateTimeFormat());
}


BOOST_AUTO_TEST_CASE(LogFormatResolutionTestDefaultFormat)
{
    LogFormat defaultFormat(Level::Info, ELPP_LITERAL("%logger %datetime %thread"));
    BOOST_CHECK_EQUAL(ELPP_LITERAL("%logger %datetime %thread"), defaultFormat.userFormat());
    BOOST_CHECK_EQUAL(ELPP_LITERAL("%logger %datetime %thread"), defaultFormat.format());
    BOOST_CHECK_EQUAL("%Y-%M-%d %H:%m:%s,%g", defaultFormat.dateTimeFormat());

    LogFormat defaultFormat2(Level::Info, ELPP_LITERAL("%logger %datetime %thread"));
    BOOST_CHECK_EQUAL(ELPP_LITERAL("%logger %datetime %thread"), defaultFormat2.userFormat());
    BOOST_CHECK_EQUAL(ELPP_LITERAL("%logger %datetime %thread"), defaultFormat2.format());
    BOOST_CHECK_EQUAL("%Y-%M-%d %H:%m:%s,%g", defaultFormat2.dateTimeFormat());

    LogFormat defaultFormat4(
        Level::Verbose, ELPP_LITERAL("%logger %level-%vlevel %datetime %thread"));
    BOOST_CHECK_EQUAL(
        ELPP_LITERAL("%logger %level-%vlevel %datetime %thread"), defaultFormat4.userFormat());
    BOOST_CHECK_EQUAL(
        ELPP_LITERAL("%logger VERBOSE-%vlevel %datetime %thread"), defaultFormat4.format());
    BOOST_CHECK_EQUAL("%Y-%M-%d %H:%m:%s,%g", defaultFormat4.dateTimeFormat());
}

BOOST_AUTO_TEST_CASE(LogFormatResolutionTestEscapedFormat)
{
    SubsecondPrecision ssPrec(3);

    LogFormat escapeTest(Level::Info, ELPP_LITERAL("%logger %datetime{%%H %H} %thread"));
    BOOST_CHECK_EQUAL(ELPP_LITERAL("%logger %datetime{%%H %H} %thread"), escapeTest.userFormat());
    BOOST_CHECK_EQUAL(ELPP_LITERAL("%logger %datetime %thread"), escapeTest.format());
    BOOST_CHECK_EQUAL("%%H %H", escapeTest.dateTimeFormat());
    BOOST_CHECK(
        Str::startsWith(DateTime::getDateTime(escapeTest.dateTimeFormat().c_str(), &ssPrec), "%H"));

    LogFormat escapeTest2(
        Level::Info, ELPP_LITERAL("%%logger %%datetime{%%H %H %%H} %%thread %thread %%thread"));
    BOOST_CHECK_EQUAL(ELPP_LITERAL("%%logger %%datetime{%%H %H %%H} %%thread %thread %%thread"),
        escapeTest2.userFormat());
    BOOST_CHECK_EQUAL(ELPP_LITERAL("%%logger %%datetime{%%H %H %%H} %%thread %thread %thread"),
        escapeTest2.format());
    BOOST_CHECK_EQUAL("", escapeTest2.dateTimeFormat());  // Date/time escaped
    BOOST_CHECK(
        Str::startsWith(DateTime::getDateTime(escapeTest.dateTimeFormat().c_str(), &ssPrec), "%H"));

    LogFormat escapeTest3(
        Level::Info, ELPP_LITERAL("%%logger %datetime{%%H %H %%H} %%thread %thread %%thread"));
    BOOST_CHECK_EQUAL(ELPP_LITERAL("%%logger %datetime{%%H %H %%H} %%thread %thread %%thread"),
        escapeTest3.userFormat());
    BOOST_CHECK_EQUAL(
        ELPP_LITERAL("%%logger %datetime %%thread %thread %thread"), escapeTest3.format());
    BOOST_CHECK_EQUAL("%%H %H %%H", escapeTest3.dateTimeFormat());  // Date/time escaped
    BOOST_CHECK(
        Str::startsWith(DateTime::getDateTime(escapeTest.dateTimeFormat().c_str(), &ssPrec), "%H"));
}

class Integer : public el::Loggable
{
public:
    Integer(int i) : m_underlyingInt(i) {}
    Integer& operator=(const Integer& integer)
    {
        m_underlyingInt = integer.m_underlyingInt;
        return *this;
    }
    virtual ~Integer(void) {}
    inline operator int() const { return m_underlyingInt; }
    inline void operator++() { ++m_underlyingInt; }
    inline void operator--() { --m_underlyingInt; }
    inline bool operator==(const Integer& integer) const
    {
        return m_underlyingInt == integer.m_underlyingInt;
    }
    void inline log(el::base::type::ostream_t& os) const { os << m_underlyingInt; }

private:
    int m_underlyingInt;
};


BOOST_AUTO_TEST_CASE(LoggableTestTestValidLog)
{
    Integer myint = 5;
    LOG(INFO) << "My integer = " << myint;
    std::string expected = BUILD_STR(getDate() << " My integer = 5\n");
    BOOST_CHECK_EQUAL(expected, tail(1));
    ++myint;
    LOG(INFO) << "My integer = " << myint;
    expected = BUILD_STR(getDate() << " My integer = 6\n");
    BOOST_CHECK_EQUAL(expected, tail(1));
}

class String
{
public:
    String(const char* s) : m_str(s) {}
    const char* c_str(void) const { return m_str; }

private:
    const char* m_str;
};

inline MAKE_LOGGABLE(String, str, os)
{
    os << str.c_str();
    return os;
}

BOOST_AUTO_TEST_CASE(LoggableTestMakeLoggable)
{
    LOG(INFO) << String("this is my string");
    std::string expected = BUILD_STR(getDate() << " this is my string\n");
    BOOST_CHECK_EQUAL(expected, tail(1));
}

BOOST_AUTO_TEST_CASE(LoggerTestValidId)
{
    BOOST_CHECK(Logger::isValidId("a-valid-id"));
    BOOST_CHECK(!Logger::isValidId("a valid id"));
    BOOST_CHECK(!Logger::isValidId("logger-with-space-at-end "));
    BOOST_CHECK(Logger::isValidId("logger_with_no_space_at_end"));
    BOOST_CHECK(Logger::isValidId("my-great-logger-with-number-1055"));
}

BOOST_AUTO_TEST_CASE(MacrosTestVaLength)
{
    BOOST_CHECK_EQUAL(10, el_getVALength("a", "b", "c", "d", "e", "f", "g", "h", "i", "j"));
    BOOST_CHECK_EQUAL(9, el_getVALength("a", "b", "c", "d", "e", "f", "g", "h", "i"));
    BOOST_CHECK_EQUAL(8, el_getVALength("a", "b", "c", "d", "e", "f", "g", "h"));
    BOOST_CHECK_EQUAL(7, el_getVALength("a", "b", "c", "d", "e", "f", "g"));
    BOOST_CHECK_EQUAL(6, el_getVALength("a", "b", "c", "d", "e", "f"));
    BOOST_CHECK_EQUAL(5, el_getVALength("a", "b", "c", "d", "e"));
    BOOST_CHECK_EQUAL(4, el_getVALength("a", "b", "c", "d"));
    BOOST_CHECK_EQUAL(3, el_getVALength("a", "b", "c"));
    BOOST_CHECK_EQUAL(2, el_getVALength("a", "b"));
    BOOST_CHECK_EQUAL(1, el_getVALength("a"));
}

#if ELPP_OS_UNIX
BOOST_AUTO_TEST_CASE(OSUtilsTestGetBashOutput)
{
    const char* bashCommand = "echo 'test'";
    std::string bashResult = OS::getBashOutput(bashCommand);
    BOOST_CHECK_EQUAL("test", bashResult);
}
#endif

BOOST_AUTO_TEST_CASE(OSUtilsTestGetEnvironmentVariable)
{
    std::string variable = OS::getEnvironmentVariable("PATH", "pathResult");
    BOOST_CHECK(!variable.empty());
}

BOOST_AUTO_TEST_CASE(PLogTestWriteLog)
{
    std::fstream file("/tmp/a/file/that/does/not/exist.txt", std::fstream::in);
    if (file.is_open())
    {
        // We dont expect to open file
        BOOST_CHECK(false);
    }
    PLOG(INFO) << "This is plog";
    std::string expected = BUILD_STR(getDate() << " This is plog: No such file or directory [2]\n");
    std::string actual = tail(1);
    BOOST_CHECK_EQUAL(expected, actual);
}

static std::vector<el::base::type::string_t> loggedMessages;

class LogHandler : public el::LogDispatchCallback
{
public:
    void handle(const LogDispatchData* data)
    {
        loggedMessages.push_back(data->logMessage()->message());
    }
};

BOOST_AUTO_TEST_CASE(LogDispatchCallbackTestInstallation)
{
    LOG(INFO) << "Log before handler installed";
    BOOST_CHECK(loggedMessages.empty());

    // Install handler
    Helpers::installLogDispatchCallback<LogHandler>("LogHandler");
    LOG(INFO) << "Should be part of loggedMessages - 1";
    BOOST_CHECK_EQUAL(1, loggedMessages.size());
    type::string_t expectedMessage = ELPP_LITERAL("Should be part of loggedMessages - 1");
    BOOST_CHECK_EQUAL(expectedMessage, loggedMessages.at(0));
}

BOOST_AUTO_TEST_CASE(LogDispatchCallbackTestUninstallation)
{
    // Uninstall handler
    Helpers::uninstallLogDispatchCallback<LogHandler>("LogHandler");
    LOG(INFO) << "This is not in list";
    BOOST_CHECK(loggedMessages.end() == std::find(loggedMessages.begin(), loggedMessages.end(),
                                            ELPP_LITERAL("This is not in list")));
}

class Person
{
public:
    Person(const std::string& name, unsigned int num) : m_name(name), m_num(num) {}
    const std::string& name(void) const { return m_name; }
    unsigned int num(void) const { return m_num; }

private:
    std::string m_name;
    unsigned int m_num;
};

class PersonPred
{
public:
    PersonPred(const std::string& name, unsigned int num) : name(name), n(num) {}
    bool operator()(const Person* p) { return p != nullptr && p->name() == name && p->num() == n; }

private:
    std::string name;
    unsigned int n;
};

class People : public Registry<Person>
{
public:
    void regNew(const char* name, Person* person) { Registry<Person>::registerNew(name, person); }
    void clear() { Registry<Person>::unregisterAll(); }
    Person* getPerson(const char* name) { return Registry<Person>::get(name); }
};

class PeopleWithPred : public RegistryWithPred<Person, PersonPred>
{
public:
    void regNew(Person* person) { RegistryWithPred<Person, PersonPred>::registerNew(person); }
    void clear() { RegistryWithPred<Person, PersonPred>::unregisterAll(); }
    Person* get(const std::string& name, unsigned int numb)
    {
        return RegistryWithPred<Person, PersonPred>::get(name, numb);
    }
};

/// Tests for usage of registry (Thread unsafe but its OK with gtest)
BOOST_AUTO_TEST_CASE(RegistryTestRegisterAndUnregister)
{
    People people;
    Person* john = new Person("John", 433212345);
    people.regNew("John", john);

    Person* john2 = new Person("John", 123456);
    people.regNew("John", john2);

    BOOST_CHECK_EQUAL(1, people.size());
    unsigned int n = people.getPerson("John")->num();
    BOOST_CHECK_EQUAL(n, 123456);

    People people2;
    people2 = people;
    BOOST_CHECK_EQUAL(1, people2.size());
    BOOST_CHECK_EQUAL(1, people.size());

    people.clear();
    BOOST_CHECK(people.empty());
    BOOST_CHECK_EQUAL(1, people2.size());
    people2.clear();
    BOOST_CHECK(people2.empty());

    PeopleWithPred peopleWithPred;
    peopleWithPred.regNew(new Person("McDonald", 123));
    peopleWithPred.regNew(new Person("McDonald", 157));
    BOOST_CHECK_EQUAL(peopleWithPred.size(), 2);

    Person* p = peopleWithPred.get("McDonald", 157);
    BOOST_CHECK_EQUAL(p->name(), "McDonald");
    BOOST_CHECK_EQUAL(p->num(), 157);

    PeopleWithPred peopleWithPred2;
    peopleWithPred2 = peopleWithPred;
    BOOST_CHECK_EQUAL(peopleWithPred.size(), 2);
    BOOST_CHECK_EQUAL(peopleWithPred2.size(), 2);

    peopleWithPred.clear();
    BOOST_CHECK(peopleWithPred.empty());
    peopleWithPred2.clear();
    BOOST_CHECK(peopleWithPred2.empty());
}

static bool handlerCalled;

void handler(const char*, std::size_t)
{
    handlerCalled = true;
}

BOOST_AUTO_TEST_CASE(StrictFileSizeCheckTestHandlerCalled)
{
    BOOST_CHECK(!handlerCalled);
    BOOST_CHECK(ELPP->hasFlag(LoggingFlag::StrictLogFileSizeCheck));

    el::Loggers::getLogger("handler_check_logger");
    el::Loggers::reconfigureLogger(
        "handler_check_logger", el::ConfigurationType::Filename, "/tmp/logs/max-size.log");
    el::Loggers::reconfigureLogger(
        "handler_check_logger", el::ConfigurationType::MaxLogFileSize, "100");
    el::Helpers::installPreRollOutCallback(handler);
    for (int i = 0; i < 100; ++i)
    {
        CLOG(INFO, "handler_check_logger") << "Test " << i;
    }
    BOOST_CHECK(handlerCalled);
}


BOOST_AUTO_TEST_CASE(StringUtilsTestWildCardMatch)
{
    BOOST_CHECK(Str::wildCardMatch("main", "m*"));
    BOOST_CHECK(Str::wildCardMatch("mei.cpp", "m*cpp"));
    BOOST_CHECK(Str::wildCardMatch("me.cpp", "me.cpp"));
    BOOST_CHECK(Str::wildCardMatch("me.cpp", "m?.cpp"));
    BOOST_CHECK(Str::wildCardMatch("m/f.cpp", "m??.cpp"));
    BOOST_CHECK(Str::wildCardMatch("", "*"));
    BOOST_CHECK(!Str::wildCardMatch("", "?"));
    BOOST_CHECK(Str::wildCardMatch(
        "fastquery--and anything after or before", "*****************fast*****query*****"));
    BOOST_CHECK(!Str::wildCardMatch("some thing not matching", "some * matching all"));
}

BOOST_AUTO_TEST_CASE(StringUtilsTestTrim)
{
    std::string strLeftWhiteSpace(" string 1");
    std::string strLeftRightWhiteSpace(" string 2 ");
    std::string strRightWhiteSpace("string 3 ");
    std::string strLeftRightWhiteSpaceWithTabAndNewLine("  string 4 \t\n");
    BOOST_CHECK_EQUAL("string 1", Str::trim(strLeftWhiteSpace));
    BOOST_CHECK_EQUAL("string 2", Str::trim(strLeftRightWhiteSpace));
    BOOST_CHECK_EQUAL("string 3", Str::trim(strRightWhiteSpace));
    BOOST_CHECK_EQUAL("string 4", Str::trim(strLeftRightWhiteSpaceWithTabAndNewLine));
}

BOOST_AUTO_TEST_CASE(StringUtilsTestStartsAndEndsWith)
{
    BOOST_CHECK(Str::startsWith("Dotch this", "Dotch"));
    BOOST_CHECK(!Str::startsWith("Dotch this", "dotch"));
    BOOST_CHECK(!Str::startsWith("    Dotch this", "dotch"));
    BOOST_CHECK(Str::endsWith("Dotch this", "this"));
    BOOST_CHECK(!Str::endsWith("Dotch this", "This"));
}

BOOST_AUTO_TEST_CASE(StringUtilsTestReplaceAll)
{
    std::string str = "This is cool";
    char replaceWhat = 'o';
    char replaceWith = '0';
    BOOST_CHECK_EQUAL("This is c00l", Str::replaceAll(str, replaceWhat, replaceWith));
}

BOOST_AUTO_TEST_CASE(StringUtilsTestToUpper)
{
    std::string str = "This iS c0ol";
    BOOST_CHECK_EQUAL("THIS IS C0OL", Str::toUpper(str));
    str = "enabled = ";
    BOOST_CHECK_EQUAL("ENABLED = ", Str::toUpper(str));
}

BOOST_AUTO_TEST_CASE(StringUtilsTestCStringEq)
{
    BOOST_CHECK(Str::cStringEq("this", "this"));
    BOOST_CHECK(!Str::cStringEq(nullptr, "this"));
    BOOST_CHECK(Str::cStringEq(nullptr, nullptr));
}

BOOST_AUTO_TEST_CASE(StringUtilsTestCStringCaseEq)
{
    BOOST_CHECK(Str::cStringCaseEq("this", "This"));
    BOOST_CHECK(Str::cStringCaseEq("this", "this"));
    BOOST_CHECK(Str::cStringCaseEq(nullptr, nullptr));
    BOOST_CHECK(!Str::cStringCaseEq(nullptr, "nope"));
}

BOOST_AUTO_TEST_CASE(StringUtilsTestContains)
{
    BOOST_CHECK(Str::contains("the quick brown fox jumped over the lazy dog", 'a'));
    BOOST_CHECK(!Str::contains("the quick brown fox jumped over the lazy dog", '9'));
}

BOOST_AUTO_TEST_CASE(StringUtilsTestReplaceFirstWithEscape)
{
    el::base::type::string_t str = ELPP_LITERAL("Rolling in the deep");
    Str::replaceFirstWithEscape(str, ELPP_LITERAL("Rolling"), ELPP_LITERAL("Swimming"));
    BOOST_CHECK_EQUAL(ELPP_LITERAL("Swimming in the deep"), str);
    str = ELPP_LITERAL("this is great and this is not");
    Str::replaceFirstWithEscape(str, ELPP_LITERAL("this is"), ELPP_LITERAL("that was"));
    BOOST_CHECK_EQUAL(ELPP_LITERAL("that was great and this is not"), str);
    str = ELPP_LITERAL("%this is great and this is not");
    Str::replaceFirstWithEscape(str, ELPP_LITERAL("this is"), ELPP_LITERAL("that was"));
    BOOST_CHECK_EQUAL(ELPP_LITERAL("this is great and that was not"), str);
    str = ELPP_LITERAL("%datetime %level %msg");
    Str::replaceFirstWithEscape(
        str, ELPP_LITERAL("%level"), LevelHelper::convertToString(Level::Info));
    BOOST_CHECK_EQUAL(ELPP_LITERAL("%datetime INFO %msg"), str);
}

BOOST_AUTO_TEST_CASE(StringUtilsTestAddToBuff)
{
    char buf[100];
    char* bufLim = buf + 100;
    char* buffPtr = buf;

    buffPtr = Str::addToBuff("The quick brown fox", buffPtr, bufLim);
    BOOST_CHECK_EQUAL("The quick brown fox", buf);
    buffPtr = Str::addToBuff(" jumps over the lazy dog", buffPtr, bufLim);
    BOOST_CHECK_EQUAL("The quick brown fox jumps over the lazy dog", buf);
}

BOOST_AUTO_TEST_CASE(StringUtilsTestConvertAndAddToBuff)
{
    char buf[100];
    char* bufLim = buf + 100;
    char* buffPtr = buf;

    buffPtr = Str::addToBuff("Today is lets say ", buffPtr, bufLim);
    buffPtr = Str::convertAndAddToBuff(5, 1, buffPtr, bufLim);
    buffPtr = Str::addToBuff(" but we write it as ", buffPtr, bufLim);
    buffPtr = Str::convertAndAddToBuff(5, 2, buffPtr, bufLim);
    BOOST_CHECK_EQUAL("Today is lets say 5 but we write it as 05", buf);
}

static const char* kSysLogFile = "/var/log/syslog";
static const char* s_currentHost = el::base::utils::OS::currentHost().c_str();

BOOST_AUTO_TEST_CASE(SysLogTestWriteLog)
{
    if (!fileExists(kSysLogFile))
    {
        // Do not check for syslog config, just dont test it
        return;
    }
    // To avoid "Easylogging++ last message repeated 2 times"
    SYSLOG(INFO) << "last message suppress";

    SYSLOG(INFO) << "this is my syslog";
    sleep(1);  // Make sure daemon has picked it up
    std::string expectedEnd =
        BUILD_STR(s_currentHost << " " << kSysLogIdent << ": INFO : this is my syslog\n");
    std::string actual = tail(1, kSysLogFile);
    BOOST_CHECK(Str::endsWith(actual, expectedEnd));
}

BOOST_AUTO_TEST_CASE(SysLogTestDebugVersionLogs)
{
    if (!fileExists(kSysLogFile))
    {
        // Do not check for syslog config, just dont test it
        return;
    }
// Test enabled
#undef ELPP_DEBUG_LOG
#define ELPP_DEBUG_LOG 0

    std::string currentTail = tail(1, kSysLogFile);

    DSYSLOG(INFO) << "No DSYSLOG should be resolved";
    sleep(1);  // Make sure daemon has picked it up
    BOOST_CHECK(Str::endsWith(currentTail, tail(1, kSysLogFile)));

    DSYSLOG_IF(true, INFO) << "No DSYSLOG_IF should be resolved";
    sleep(1);  // Make sure daemon has picked it up
    BOOST_CHECK(Str::endsWith(currentTail, tail(1, kSysLogFile)));

    DCSYSLOG(INFO, "performance") << "No DCSYSLOG should be resolved";
    sleep(1);  // Make sure daemon has picked it up
    BOOST_CHECK(Str::endsWith(currentTail, tail(1, kSysLogFile)));

    DCSYSLOG(INFO, "performance") << "No DCSYSLOG should be resolved";
    sleep(1);  // Make sure daemon has picked it up
    BOOST_CHECK(Str::endsWith(currentTail, tail(1, kSysLogFile)));

// Reset
#undef ELPP_DEBUG_LOG
#define ELPP_DEBUG_LOG 1

    // Now test again
    DSYSLOG(INFO) << "DSYSLOG should be resolved";
    sleep(1);  // Make sure daemon has picked it up
    std::string expected =
        BUILD_STR(s_currentHost << " " << kSysLogIdent << ": INFO : DSYSLOG should be resolved\n");
    // BOOST_CHECK(Str::endsWith(tail(1, kSysLogFile), expected));

    DSYSLOG_IF(true, INFO) << "DSYSLOG_IF should be resolved";
    sleep(1);  // Make sure daemon has picked it up
    expected = BUILD_STR(
        s_currentHost << " " << kSysLogIdent << ": INFO : DSYSLOG_IF should be resolved\n");
    // BOOST_CHECK(Str::endsWith(tail(1, kSysLogFile), expected));

    DCSYSLOG(INFO, el::base::consts::kSysLogLoggerId) << "DCSYSLOG should be resolved";
    sleep(1);  // Make sure daemon has picked it up
    expected =
        BUILD_STR(s_currentHost << " " << kSysLogIdent << ": INFO : DCSYSLOG should be resolved\n");
    // BOOST_CHECK(Str::endsWith(tail(1, kSysLogFile), expected));

    DCSYSLOG(INFO, el::base::consts::kSysLogLoggerId) << "DCSYSLOG should be resolved";
    sleep(1);  // Make sure daemon has picked it up
    expected =
        BUILD_STR(s_currentHost << " " << kSysLogIdent << ": INFO : DCSYSLOG should be resolved\n");
    // BOOST_CHECK(Str::endsWith(tail(1, kSysLogFile), expected));
}


const char* getConfFile(void)
{
    const char* file = "/tmp/temp-test.conf";
    std::fstream confFile(file, std::fstream::out);
    confFile << " * GLOBAL:\n"
             << "    FILENAME             =  /tmp/my-test.log\n"
             << "    FORMAT               =  %datetime %level %msg\n"
             << "    MAX_LOG_FILE_SIZE        =  1\n"
             << "    TO_STANDARD_OUTPUT   =  TRUE\n"
             << "* DEBUG:\n"
             // NOTE escaped %level and %host below
             << "    FORMAT               =  %datetime %%level %level [%user@%%host] [%func] "
                "[%loc] %msg\n"
             // INFO and WARNING uses is defined by GLOBAL
             << "* ERROR:\n"
             << "    FILENAME             =  /tmp/my-test-err.log\n"
             << "    FORMAT               =  %%logger %%logger %logger %%logger %msg\n"
             << "    MAX_LOG_FILE_SIZE        =  10\n"
             << "* FATAL:\n"
             << "    FORMAT               =  %datetime %%datetime{%H:%m} %level %msg\n"
             << "* VERBOSE:\n"
             << "    FORMAT               =  %%datetime{%h:%m} %datetime %level-%vlevel %msg\n"
             << "* TRACE:\n"
             << "    FORMAT               =  %datetime{%h:%m} %%level %level [%func] [%loc] %msg\n";
    confFile.close();
    return file;
}

BOOST_AUTO_TEST_CASE(TypedConfigurationsTestInitialization)
{
    std::string testFile = "/tmp/my-test.log";
    std::remove(testFile.c_str());

    Configurations c(getConfFile());
    TypedConfigurations tConf(&c, ELPP->registeredLoggers()->logStreamsReference());

    BOOST_CHECK(tConf.enabled(Level::Global));

    BOOST_CHECK_EQUAL(
        ELPP_LITERAL("%datetime %level %msg"), tConf.logFormat(Level::Info).userFormat());
    BOOST_CHECK_EQUAL(ELPP_LITERAL("%datetime INFO %msg"), tConf.logFormat(Level::Info).format());
    BOOST_CHECK_EQUAL("%Y-%M-%d %H:%m:%s,%g", tConf.logFormat(Level::Info).dateTimeFormat());

    BOOST_CHECK_EQUAL(ELPP_LITERAL("%datetime %%level %level [%user@%%host] [%func] [%loc] %msg"),
        tConf.logFormat(Level::Debug).userFormat());
    std::string expected =
        BUILD_STR("%datetime %level DEBUG [" << el::base::utils::OS::currentUser()
                                             << "@%%host] [%func] [%loc] %msg");
#if defined(ELPP_UNICODE)
    char* orig = Str::wcharPtrToCharPtr(tConf.logFormat(Level::Debug).format().c_str());
#else
    const char* orig = tConf.logFormat(Level::Debug).format().c_str();
#endif
    BOOST_CHECK_EQUAL(expected, std::string(orig));
    BOOST_CHECK_EQUAL("%Y-%M-%d %H:%m:%s,%g", tConf.logFormat(Level::Debug).dateTimeFormat());

    // This double quote is escaped at run-time for %date and %datetime
    BOOST_CHECK_EQUAL(ELPP_LITERAL("%datetime %%datetime{%H:%m} %level %msg"),
        tConf.logFormat(Level::Fatal).userFormat());
    BOOST_CHECK_EQUAL(ELPP_LITERAL("%datetime %%datetime{%H:%m} FATAL %msg"),
        tConf.logFormat(Level::Fatal).format());
    BOOST_CHECK_EQUAL("%Y-%M-%d %H:%m:%s,%g", tConf.logFormat(Level::Fatal).dateTimeFormat());

    BOOST_CHECK_EQUAL(ELPP_LITERAL("%datetime{%h:%m} %%level %level [%func] [%loc] %msg"),
        tConf.logFormat(Level::Trace).userFormat());
    BOOST_CHECK_EQUAL(ELPP_LITERAL("%datetime %level TRACE [%func] [%loc] %msg"),
        tConf.logFormat(Level::Trace).format());
    BOOST_CHECK_EQUAL("%h:%m", tConf.logFormat(Level::Trace).dateTimeFormat());

    BOOST_CHECK_EQUAL(ELPP_LITERAL("%%datetime{%h:%m} %datetime %level-%vlevel %msg"),
        tConf.logFormat(Level::Verbose).userFormat());
    BOOST_CHECK_EQUAL(ELPP_LITERAL("%%datetime{%h:%m} %datetime VERBOSE-%vlevel %msg"),
        tConf.logFormat(Level::Verbose).format());
    BOOST_CHECK_EQUAL("%Y-%M-%d %H:%m:%s,%g", tConf.logFormat(Level::Verbose).dateTimeFormat());

    BOOST_CHECK_EQUAL(ELPP_LITERAL("%%logger %%logger %logger %%logger %msg"),
        tConf.logFormat(Level::Error).userFormat());
    BOOST_CHECK_EQUAL(ELPP_LITERAL("%%logger %%logger %logger %logger %msg"),
        tConf.logFormat(Level::Error).format());
    BOOST_CHECK_EQUAL("", tConf.logFormat(Level::Error).dateTimeFormat());
}


BOOST_AUTO_TEST_CASE(TypedConfigurationsTestSharedFileStreams)
{
    Configurations c(getConfFile());
    TypedConfigurations tConf(&c, ELPP->registeredLoggers()->logStreamsReference());
    // Make sure we have only two unique file streams for ALL and ERROR
    el::base::type::EnumType lIndex = LevelHelper::kMinValid;
    el::base::type::fstream_t* prev = nullptr;
    LevelHelper::forEachLevel(&lIndex, [&]() -> bool {
        if (prev == nullptr)
        {
            prev = tConf.fileStream(LevelHelper::castFromInt(lIndex));
        }
        else
        {
            if (LevelHelper::castFromInt(lIndex) == Level::Error)
            {
                BOOST_CHECK(prev != tConf.fileStream(LevelHelper::castFromInt(lIndex)));
            }
            else
            {
                BOOST_CHECK_EQUAL(prev, tConf.fileStream(LevelHelper::castFromInt(lIndex)));
            }
        }
        return false;
    });
}

BOOST_AUTO_TEST_CASE(TypedConfigurationsTestNonExistentFileCreation)
{
    Configurations c(getConfFile());
    c.setGlobally(ConfigurationType::Filename, "/tmp/logs/el.gtest.log");
    c.set(Level::Info, ConfigurationType::ToFile, "true");
    c.set(Level::Error, ConfigurationType::ToFile, "true");
    c.set(Level::Info, ConfigurationType::Filename, "/a/file/not/possible/to/create/log.log");
    c.set(Level::Error, ConfigurationType::Filename, "/tmp/logs/el.gtest.log");
    TypedConfigurations tConf(&c, ELPP->registeredLoggers()->logStreamsReference());
    BOOST_CHECK(tConf.toFile(Level::Global));
    BOOST_CHECK(!tConf.toFile(Level::Info));
    BOOST_CHECK(tConf.toFile(Level::Error));
    BOOST_CHECK(nullptr == tConf.fileStream(Level::Info));   // nullptr
    BOOST_CHECK(nullptr != tConf.fileStream(Level::Error));  // Not null
}

BOOST_AUTO_TEST_CASE(TypedConfigurationsTestWriteToFiles)
{
    std::string testFile = "/tmp/my-test.log";
    Configurations c(getConfFile());
    TypedConfigurations tConf(&c, ELPP->registeredLoggers()->logStreamsReference());
    {
        BOOST_CHECK(tConf.fileStream(Level::Info)->is_open());
        BOOST_CHECK_EQUAL(testFile, tConf.filename(Level::Info));
        *tConf.fileStream(Level::Info) << "-Info";
        *tConf.fileStream(Level::Debug) << "-Debug";
        tConf.fileStream(Level::Debug)->flush();
        *tConf.fileStream(Level::Error) << "-Error";
        tConf.fileStream(Level::Error)->flush();
    }
    std::ifstream reader(tConf.filename(Level::Info), std::fstream::in);
    std::string line = std::string();
    std::getline(reader, line);
    BOOST_CHECK_EQUAL("-Info-Debug", line);
}

BOOST_AUTO_TEST_CASE(UtilitiesTestSafeDelete)
{
    int* i = new int(12);
    BOOST_CHECK(i != nullptr);
    safeDelete(i);
    BOOST_CHECK(nullptr == i);
}


BOOST_AUTO_TEST_CASE(VerboseAppArgumentsTestAppArgsLevel)
{
    const char* c[10];
    c[0] = "myprog";
    c[1] = "--v=5";
    c[2] = "\0";
    el::Helpers::setArgs(2, c);
    BOOST_CHECK(VLOG_IS_ON(1));
    BOOST_CHECK(VLOG_IS_ON(2));
    BOOST_CHECK(VLOG_IS_ON(3));
    BOOST_CHECK(VLOG_IS_ON(4));
    BOOST_CHECK(VLOG_IS_ON(5));
    BOOST_CHECK(!VLOG_IS_ON(6));
    BOOST_CHECK(!VLOG_IS_ON(8));
    BOOST_CHECK(!VLOG_IS_ON(9));

    c[0] = "myprog";
    c[1] = "--v=x";  // SHOULD BE ZERO NOW!
    c[2] = "\0";
    el::Helpers::setArgs(2, c);
    BOOST_CHECK(!VLOG_IS_ON(1));
    BOOST_CHECK(!VLOG_IS_ON(2));
    BOOST_CHECK(!VLOG_IS_ON(3));
    BOOST_CHECK(!VLOG_IS_ON(4));
    BOOST_CHECK(!VLOG_IS_ON(5));
    BOOST_CHECK(!VLOG_IS_ON(6));
    BOOST_CHECK(!VLOG_IS_ON(8));
    BOOST_CHECK(!VLOG_IS_ON(9));

    c[0] = "myprog";
    c[1] = "-v";  // Sets to max level (9)
    c[2] = "\0";
    el::Helpers::setArgs(2, c);
    BOOST_CHECK(VLOG_IS_ON(1));
    BOOST_CHECK(VLOG_IS_ON(2));
    BOOST_CHECK(VLOG_IS_ON(3));
    BOOST_CHECK(VLOG_IS_ON(4));
    BOOST_CHECK(VLOG_IS_ON(5));
    BOOST_CHECK(VLOG_IS_ON(6));
    BOOST_CHECK(VLOG_IS_ON(8));
    BOOST_CHECK(VLOG_IS_ON(9));

    c[0] = "myprog";
    c[1] = "--verbose";  // Sets to max level (9)
    c[2] = "\0";
    el::Helpers::setArgs(2, c);
    BOOST_CHECK(VLOG_IS_ON(1));
    BOOST_CHECK(VLOG_IS_ON(2));
    BOOST_CHECK(VLOG_IS_ON(3));
    BOOST_CHECK(VLOG_IS_ON(4));
    BOOST_CHECK(VLOG_IS_ON(5));
    BOOST_CHECK(VLOG_IS_ON(6));
    BOOST_CHECK(VLOG_IS_ON(8));
    BOOST_CHECK(VLOG_IS_ON(9));

    // ----------------------- UPPER CASE VERSION OF SAME TEST CASES -----------------
    c[0] = "myprog";
    c[1] = "--V=5";
    c[2] = "\0";
    el::Helpers::setArgs(2, c);
    BOOST_CHECK(VLOG_IS_ON(1));
    BOOST_CHECK(VLOG_IS_ON(2));
    BOOST_CHECK(VLOG_IS_ON(3));
    BOOST_CHECK(VLOG_IS_ON(4));
    BOOST_CHECK(VLOG_IS_ON(5));
    BOOST_CHECK(!VLOG_IS_ON(6));
    BOOST_CHECK(!VLOG_IS_ON(8));
    BOOST_CHECK(!VLOG_IS_ON(9));

    c[0] = "myprog";
    c[1] = "--V=x";  // SHOULD BECOME ZERO!
    c[2] = "\0";
    el::Helpers::setArgs(2, c);
    BOOST_CHECK(!VLOG_IS_ON(1));
    BOOST_CHECK(!VLOG_IS_ON(2));
    BOOST_CHECK(!VLOG_IS_ON(3));
    BOOST_CHECK(!VLOG_IS_ON(4));
    BOOST_CHECK(!VLOG_IS_ON(5));
    BOOST_CHECK(!VLOG_IS_ON(6));
    BOOST_CHECK(!VLOG_IS_ON(8));
    BOOST_CHECK(!VLOG_IS_ON(9));

    c[0] = "myprog";
    c[1] = "-V";  // Sets to max level (9)
    c[2] = "\0";
    el::Helpers::setArgs(2, c);
    BOOST_CHECK(VLOG_IS_ON(1));
    BOOST_CHECK(VLOG_IS_ON(2));
    BOOST_CHECK(VLOG_IS_ON(3));
    BOOST_CHECK(VLOG_IS_ON(4));
    BOOST_CHECK(VLOG_IS_ON(5));
    BOOST_CHECK(VLOG_IS_ON(6));
    BOOST_CHECK(VLOG_IS_ON(8));
    BOOST_CHECK(VLOG_IS_ON(9));

    c[0] = "myprog";
    c[1] = "--VERBOSE";  // Sets to max level (9)
    c[2] = "\0";
    el::Helpers::setArgs(2, c);
    BOOST_CHECK(VLOG_IS_ON(1));
    BOOST_CHECK(VLOG_IS_ON(2));
    BOOST_CHECK(VLOG_IS_ON(3));
    BOOST_CHECK(VLOG_IS_ON(4));
    BOOST_CHECK(VLOG_IS_ON(5));
    BOOST_CHECK(VLOG_IS_ON(6));
    BOOST_CHECK(VLOG_IS_ON(8));
    BOOST_CHECK(VLOG_IS_ON(9));
}

BOOST_AUTO_TEST_CASE(VerboseAppArgumentsTestAppArgsVModules)
{
    const char* c[10];
    c[0] = "myprog";
    c[1] = "-vmodule=main*=3,easy.\?\?\?=1";
    c[2] = "\0";
    el::Helpers::setArgs(2, c);

    BOOST_CHECK((ELPP->vRegistry()->allowed(2, "main.cpp")));
    BOOST_CHECK((ELPP->vRegistry()->allowed(3, "main.h")));
    BOOST_CHECK(!(ELPP->vRegistry()->allowed(4, "main.c")));
    BOOST_CHECK(!(ELPP->vRegistry()->allowed(5, "main.cpp")));
    BOOST_CHECK((ELPP->vRegistry()->allowed(2, "main.cxx")));
    BOOST_CHECK((ELPP->vRegistry()->allowed(1, "main.cc")));
    BOOST_CHECK((ELPP->vRegistry()->allowed(3, "main-file-for-prog.cc")));

    el::Loggers::removeFlag(el::LoggingFlag::AllowVerboseIfModuleNotSpecified);  // Check strictly

    BOOST_CHECK(!(ELPP->vRegistry()->allowed(4, "tmain.cxx")));

    BOOST_CHECK(ELPP->vRegistry()->allowed(1, "easy.cpp"));
    BOOST_CHECK((ELPP->vRegistry()->allowed(1, "easy.cxx")));
    BOOST_CHECK((ELPP->vRegistry()->allowed(1, "easy.hxx")));
    BOOST_CHECK((ELPP->vRegistry()->allowed(1, "easy.hpp")));

    BOOST_CHECK(!(ELPP->vRegistry()->allowed(2, "easy.cpp")));
    BOOST_CHECK(!(ELPP->vRegistry()->allowed(2, "easy.cxx")));
    BOOST_CHECK(!(ELPP->vRegistry()->allowed(2, "easy.hxx")));
    BOOST_CHECK(!(ELPP->vRegistry()->allowed(2, "easy.hpp")));

    BOOST_CHECK(!(ELPP->vRegistry()->allowed(1, "easy.cc")));
    BOOST_CHECK(!(ELPP->vRegistry()->allowed(1, "easy.hh")));
    BOOST_CHECK(!(ELPP->vRegistry()->allowed(1, "easy.h")));
    BOOST_CHECK(!(ELPP->vRegistry()->allowed(1, "easy.c")));
}

BOOST_AUTO_TEST_CASE(VerboseAppArgumentsTestAppArgsVModulesExtension)
{
    el::Loggers::ScopedRemoveFlag scopedFlag(LoggingFlag::DisableVModulesExtensions);
    ELPP_UNUSED(scopedFlag);

    const char* c[10];
    c[0] = "myprog";
    c[1] = "-vmodule=main*=3,easy*=1";
    c[2] = "\0";
    el::Helpers::setArgs(2, c);

    BOOST_CHECK((ELPP->vRegistry()->allowed(2, "main.cpp")));
    BOOST_CHECK((ELPP->vRegistry()->allowed(3, "main.h")));
    BOOST_CHECK(!(ELPP->vRegistry()->allowed(4, "main.c")));
    BOOST_CHECK(!(ELPP->vRegistry()->allowed(5, "main.cpp")));

    BOOST_CHECK((ELPP->vRegistry()->allowed(2, "main.cxx")));
    BOOST_CHECK((ELPP->vRegistry()->allowed(1, "main.cc")));
    BOOST_CHECK((ELPP->vRegistry()->allowed(3, "main-file-for-prog.cc")));
    BOOST_CHECK(ELPP->vRegistry()->allowed(1, "easy.cpp"));
    BOOST_CHECK((ELPP->vRegistry()->allowed(1, "easy.cxx")));
    BOOST_CHECK((ELPP->vRegistry()->allowed(1, "easy.hxx")));
    BOOST_CHECK((ELPP->vRegistry()->allowed(1, "easy.hpp")));
    BOOST_CHECK((ELPP->vRegistry()->allowed(1, "easy.cc")));
    BOOST_CHECK((ELPP->vRegistry()->allowed(1, "easy.hh")));
    BOOST_CHECK((ELPP->vRegistry()->allowed(1, "easy.h")));
    BOOST_CHECK((ELPP->vRegistry()->allowed(1, "easy.c")));
    BOOST_CHECK(!(ELPP->vRegistry()->allowed(3, "easy-vector.cc")));
    BOOST_CHECK(!(ELPP->vRegistry()->allowed(2, "easy-vector.cc")));
    BOOST_CHECK((ELPP->vRegistry()->allowed(1, "easy-vector.cc")));
}

BOOST_AUTO_TEST_CASE(VerboseAppArgumentsTestVModulesClear)
{
    el::Loggers::ScopedRemoveFlag scopedFlag(LoggingFlag::DisableVModulesExtensions);
    ELPP_UNUSED(scopedFlag);

    const char* c[10];
    c[0] = "myprog";
    c[1] = "-vmodule=main*=3,easy*=1";
    c[2] = "--v=6";
    c[3] = "\0";
    el::Helpers::setArgs(3, c);

    BOOST_CHECK((ELPP->vRegistry()->allowed(2, "main.cpp")));
    // BOOST_CHECK(!(ELPP->vRegistry()->allowed(5, "main.cpp")));
    ELPP->vRegistry()->clearModules();
    BOOST_CHECK((ELPP->vRegistry()->allowed(2, "main.cpp")));
    BOOST_CHECK((ELPP->vRegistry()->allowed(5, "main.cpp")));
    BOOST_CHECK(!(ELPP->vRegistry()->allowed(7, "main.cpp")));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace dev

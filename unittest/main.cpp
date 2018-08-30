#define BOOST_TEST_MAIN

#ifndef BUILD_BOOST
#define BOOST_TEST_DYN_LINK
#endif

#define BOOST_TEST_MODULE ethereum

#include <libdevcore/easylog.h>
#include <boost/test/unit_test.hpp>

INITIALIZE_EASYLOGGINGPP
struct logConfig {
  logConfig() {
    el::Configurations conf;
    conf.set(el::Level::Global, el::ConfigurationType::ToStandardOutput,
             std::to_string(false));
    el::Loggers::reconfigureAllLoggers(conf);
  }
};

BOOST_GLOBAL_FIXTURE(logConfig);

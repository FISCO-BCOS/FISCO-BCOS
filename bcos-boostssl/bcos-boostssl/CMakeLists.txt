
file(GLOB_RECURSE SRCS "*.cpp")
file(GLOB_RECURSE HEADERS "*.h")

add_library(${BOOSTSSL_TARGET} ${SRCS} ${HEADERS})
target_link_libraries(${BOOSTSSL_TARGET} PUBLIC tassl::ssl tassl::crypto ${UTILITIES_TARGET} bcos-framework Boost::system Boost::filesystem
Boost::log Boost::chrono Boost::iostreams Boost::thread )
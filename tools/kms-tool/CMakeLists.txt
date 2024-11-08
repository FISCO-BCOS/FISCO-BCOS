# file(GLOB SRC_LIST "*.cpp")

# aws sdk
# find_package(${SECURITY_TARGET} REQUIRED)
find_package(AWSSDK REQUIRED COMPONENTS kms)
add_executable(kms_tool KmsTool.cpp)
target_link_libraries(kms_tool PUBLIC ${SECURITY_TARGET})
# target_link_libraries(kms_tool
#     PUBLIC
#     AWSSDK
# )

find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

# link libraries
target_link_libraries(kms_tool PRIVATE
    ${AWSSDK_LINK_LIBRARIES}
    ZLIB::ZLIB
    OpenSSL::SSL
    OpenSSL::Crypto
    CURL::libcurl
)

# add include directories
target_include_directories(kms_tool PRIVATE
    ${AWSSDK_INCLUDE_DIRS}
)
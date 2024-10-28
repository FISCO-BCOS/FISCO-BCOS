# file(GLOB SRC_LIST "*.cpp")

# AWS SDK需要的包
find_package(AWSSDK REQUIRED COMPONENTS kms)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

# 添加源文件
add_executable(kms_tool
    KmsTool.cpp
    Utils.cpp
    KmsWrapper.h
    AwsKmsWrapper.cpp
)

# 链接AWS SDK库
target_link_libraries(kms_tool PRIVATE
    ${AWSSDK_LINK_LIBRARIES}
    ZLIB::ZLIB
    OpenSSL::SSL
    OpenSSL::Crypto
    CURL::libcurl
)

# 添加头文件搜索路径
target_include_directories(kms_tool PRIVATE
    ${AWSSDK_INCLUDE_DIRS}
)
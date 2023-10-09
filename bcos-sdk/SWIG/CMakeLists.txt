include(UseSWIG)
include(UseJava)

add_library(bcos_swig_helper Helper.cpp)
target_link_libraries(bcos_swig_helper PUBLIC ${BCOS_CPP_SDK_TARGET} bcos-framework bcos-utilities)

find_package(SWIG 4.1 REQUIRED COMPONENTS python)
set_property(SOURCE BCOS.i PROPERTY CPLUSPLUS ON)

find_package(Python3 COMPONENTS Development REQUIRED)
swig_add_library(bcos_swig_python
    TYPE SHARED
    LANGUAGE python
    SOURCES BCOS.i
    OUTPUT_DIR bcos_sdk_python
)
target_link_libraries(bcos_swig_python PRIVATE ${BCOS_CPP_SDK_TARGET} ${TARS_PROTOCOL_TARGET} bcos_swig_helper Python3::Python)

find_package(JNI REQUIRED)
find_package(Java REQUIRED)
swig_add_library(bcos_swig_java
    TYPE SHARED
    LANGUAGE java
    SOURCES BCOS.i
    OUTPUT_DIR bcos_sdk_java
)
set_property(TARGET bcos_swig_java PROPERTY SWIG_COMPILE_OPTIONS -package org.fisco.bcos.sdk.tars)

string(TOLOWER ${CMAKE_SYSTEM_NAME} os_name)
set(triplet "${os_name}-${CMAKE_SYSTEM_PROCESSOR}")
set_property(TARGET bcos_swig_java PROPERTY LIBRARY_OUTPUT_DIRECTORY "jni/${triplet}/")

target_link_libraries(bcos_swig_java PRIVATE bcos_swig_helper ${BCOS_CPP_SDK_TARGET} ${TARS_PROTOCOL_TARGET} bcos-framework JNI::JNI)
add_custom_command(TARGET bcos_swig_java PRE_BUILD
    COMMAND mkdir -p "jni/${triplet}/"
)
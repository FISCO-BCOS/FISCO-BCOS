include(UseSWIG)
include(UseJava)

find_package(SWIG 4.1 REQUIRED COMPONENTS python)
set_property(SOURCE BCOS.i PROPERTY CPLUSPLUS ON)

find_package(Python3 COMPONENTS Development REQUIRED)
swig_add_library(bcos_swig_python
    TYPE SHARED
    LANGUAGE python
    SOURCES BCOS.i
    OUTPUT_DIR bcos_sdk_python
)
target_link_libraries(bcos_swig_python PRIVATE ${BCOS_CPP_SDK_TARGET} ${TARS_PROTOCOL_TARGET} bcos-framework Python3::Python)

find_package(JNI REQUIRED)
find_package(Java REQUIRED)
swig_add_library(bcos_swig_java
    TYPE SHARED
    LANGUAGE java
    SOURCES BCOS.i
    OUTPUT_DIR bcos_sdk_java
)
set_property(TARGET bcos_swig_java PROPERTY SWIG_COMPILE_OPTIONS -package org.fisco.bcos.sdk.tars)
target_link_libraries(bcos_swig_java PRIVATE ${BCOS_CPP_SDK_TARGET} ${TARS_PROTOCOL_TARGET} bcos-framework JNI::JNI)

FILE(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bcos_sdk_java_classes/")
FILE(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bcos_sdk_java_classes/META-INFO")
ADD_CUSTOM_COMMAND(TARGET bcos_swig_java POST_BUILD
    COMMAND "${Java_JAVAC_EXECUTABLE}" -d "${CMAKE_CURRENT_BINARY_DIR}/bcos_sdk_java_classes/" ${CMAKE_CURRENT_BINARY_DIR}/bcos_sdk_java/*.java
    COMMAND "${Java_JAR_EXECUTABLE}" -cfM bcos_sdk_java_jni.jar -C "${CMAKE_CURRENT_BINARY_DIR}/bcos_sdk_java_classes" .
)
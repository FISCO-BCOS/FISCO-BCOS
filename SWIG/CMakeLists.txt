include(UseSWIG)

find_package(SWIG 4.1 COMPONENTS python)
set_property(SOURCE BCOS.i PROPERTY CPLUSPLUS ON)

find_package(Python3 COMPONENTS Development REQUIRED)
swig_add_library(bcos_swig_python
    TYPE SHARED
    LANGUAGE python
    SOURCES BCOS.i
    OUTPUT_DIR bcos_sdk_python

)
target_link_libraries(bcos_swig_python PRIVATE ${TARS_PROTOCOL_TARGET} bcos-framework Python3::Python)

find_package(JNI REQUIRED)
swig_add_library(bcos_swig_java
    TYPE SHARED
    LANGUAGE java
    SOURCES BCOS.i
    OUTPUT_DIR bcos_sdk_java
)
target_link_libraries(bcos_swig_java PRIVATE ${TARS_PROTOCOL_TARGET} bcos-framework JNI::JNI)
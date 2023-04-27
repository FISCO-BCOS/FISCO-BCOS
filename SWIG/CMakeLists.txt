include(UseSWIG)

find_package(SWIG 4.1 COMPONENTS python)
set_property(SOURCE BCOS.i PROPERTY CPLUSPLUS ON)

find_package(Python3 COMPONENTS Development REQUIRED)
swig_add_library(bcos_swig_python
    TYPE SHARED
    LANGUAGE python
    SOURCES BCOS.i
)
target_link_libraries(bcos_swig_python PRIVATE ${TARS_PROTOCOL_TARGET} bcos-framework Python3::Python)
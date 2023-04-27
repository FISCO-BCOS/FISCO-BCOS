set_property(SOURCE BCOS.i PROPERTY CPLUSPLUS ON)
swig_add_library(bcos-swig
    SHARED
    LANGUAGE python
    SOURCES BCOS.i
)
target_link_libraries(bcos-swig PRIVATE bcos-framework)
add_compile_options("-Wno-extra")  # Override -Wextra, I don't know better option.

add_library(example-vm STATIC EXCLUDE_FROM_ALL examplevm.c)
target_include_directories(example-vm PRIVATE ../include)

add_executable(example-capi EXCLUDE_FROM_ALL capi.c)
target_include_directories(example-capi PRIVATE ../include)
target_link_libraries(example-capi PRIVATE example-vm)

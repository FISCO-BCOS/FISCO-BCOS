file(GLOB sources "*.cpp" "*.h")

add_library(storagestate ${sources})

target_link_libraries(storagestate PUBLIC storage ethcore devcore executivecontext)

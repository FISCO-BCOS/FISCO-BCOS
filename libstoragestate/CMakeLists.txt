file(GLOB sources "*.cpp" "*.h")

add_library(storagestate ${sources})

target_link_libraries(storagestate PRIVATE storage ethcore)
# target_link_libraries(storagestate PUBLIC )

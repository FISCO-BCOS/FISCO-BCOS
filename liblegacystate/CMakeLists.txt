file(GLOB sources "*.cpp" "*.h")

add_library(legacystate ${sources})

target_link_libraries(legacystate PUBLIC ethcore devcore)

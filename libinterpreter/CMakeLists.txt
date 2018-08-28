file(GLOB sources "*.cpp" "*.h")

add_library(interpreter ${sources})

target_link_libraries(interpreter PUBLIC devcore)
target_link_libraries(interpreter PRIVATE evmc::evmc evmc::instructions)


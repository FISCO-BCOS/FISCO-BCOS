file(GLOB sources "*.cpp" "*.h")

add_library(executivecontext ${sources})

target_link_libraries(executivecontext PUBLIC mptstate evmc)

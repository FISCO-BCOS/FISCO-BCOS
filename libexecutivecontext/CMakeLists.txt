file(GLOB sources "*.cpp" "*.h")

add_library(executivecontext ${sources})

target_link_libraries(executivecontext PUBLIC mptstate evmc devcore)
target_link_libraries(executivecontext PRIVATE Boost::Thread)

file(GLOB sources "*.cpp" "*.h")

add_library(mptstate ${sources})

target_link_libraries(mptstate PUBLIC ethcore devcore)

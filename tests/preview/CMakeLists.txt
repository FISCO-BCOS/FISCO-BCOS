add_executable(preview_coro main.cpp)
target_compile_options(preview_coro PRIVATE -fcoroutines)
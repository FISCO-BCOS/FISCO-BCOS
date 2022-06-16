add_executable(preview_coro main.cpp)
target_compile_options(preview_coro PUBLIC -fcoroutines)
# find_package(Boost REQUIRED program_options)
# target_link_libraries(storage_bench PUBLIC ${CRYPTO_TARGET} ${STORAGE_TARGET} Boost::program_options)

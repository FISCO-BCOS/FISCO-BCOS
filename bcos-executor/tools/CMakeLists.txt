

add_executable(injector inject_meter.cpp)
target_include_directories(injector PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../src/)
target_link_libraries(injector PUBLIC ${EXECUTOR_TARGET} wabt)

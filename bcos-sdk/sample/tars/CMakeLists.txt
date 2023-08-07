add_executable(performanceHelloWorld performanceHelloWorld.cpp Common.cpp)
target_link_libraries(performanceHelloWorld PUBLIC ${BCOS_CPP_SDK_TARGET} ${TARS_PROTOCOL_TARGET} bcos-crypto bcos-boostssl bcos-utilities)

add_executable(performanceTransfer performanceTransfer.cpp Common.cpp)
target_link_libraries(performanceTransfer PUBLIC ${BCOS_CPP_SDK_TARGET} ${TARS_PROTOCOL_TARGET} bcos-crypto bcos-boostssl bcos-utilities)
if (APPLE)
    find_path(JOSUTTIS_JTHREAD_INCLUDE_DIRS "condition_variable_any2.hpp")
    target_include_directories(performanceTransfer PRIVATE ${JOSUTTIS_JTHREAD_INCLUDE_DIRS})
endif()
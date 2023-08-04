set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

add_executable(performanceHelloWorld performanceHelloWorld.cpp Common.cpp)
target_link_libraries(performanceHelloWorld PUBLIC ${BCOS_CPP_SDK_TARGET} ${TARS_PROTOCOL_TARGET} bcos-crypto bcos-boostssl bcos-utilities)

add_executable(performanceTransfer performanceTransfer.cpp Common.cpp)
target_link_libraries(performanceTransfer PUBLIC ${BCOS_CPP_SDK_TARGET} ${TARS_PROTOCOL_TARGET} bcos-crypto bcos-boostssl bcos-utilities)
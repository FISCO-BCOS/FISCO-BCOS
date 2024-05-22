add_executable(performanceHelloWorld performanceHelloWorld.cpp Common.cpp)
target_include_directories(performanceHelloWorld PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/bcos-codec>)
target_link_libraries(performanceHelloWorld PUBLIC ${BCOS_CPP_SDK_TARGET} ${TARS_PROTOCOL_TARGET} ${CODEC_TARGET} bcos-crypto bcos-boostssl bcos-utilities)

add_executable(performanceTransfer performanceTransfer.cpp Common.cpp)
target_include_directories(performanceTransfer PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/bcos-codec>)
target_link_libraries(performanceTransfer PUBLIC ${BCOS_CPP_SDK_TARGET} ${TARS_PROTOCOL_TARGET} ${CODEC_TARGET} bcos-crypto bcos-boostssl bcos-utilities)

add_executable(performanceQuery performanceQuery.cpp Common.cpp)
target_include_directories(performanceQuery PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/bcos-codec>)
target_link_libraries(performanceQuery PUBLIC ${BCOS_CPP_SDK_TARGET} ${TARS_PROTOCOL_TARGET} ${CODEC_TARGET} bcos-crypto bcos-boostssl bcos-utilities)

add_executable(performanceTransfer20 performanceTransfer20.cpp Common.cpp)
target_link_libraries(performanceTransfer20 PUBLIC ${BCOS_CPP_SDK_TARGET} ${TARS_PROTOCOL_TARGET} ${CODEC_TARGET} bcos-crypto bcos-boostssl bcos-utilities)


# for tars generator
set(TARS_HEADER_DIR ${CMAKE_BINARY_DIR}/generated/bcos-tars-protocol/tars)
find_program(TARS_TARS2CPP tars2cpp REQUIRED)

file(GLOB_RECURSE TARS_INPUT "*.tars")

# generate tars
if(TARS_INPUT)
    foreach(TARS_FILE ${TARS_INPUT})
        get_filename_component(TARS_NAME ${TARS_FILE} NAME_WE)
        get_filename_component(TARS_PATH ${TARS_FILE} PATH)
        add_custom_command(
            OUTPUT ${TARS_HEADER_DIR}/${TARS_NAME}.h
            WORKING_DIRECTORY ${TARS_PATH}
            COMMAND ${TARS_TARS2CPP} ${TARS_FILE} --unjson --without-trace --dir=${TARS_HEADER_DIR}
            COMMENT "generating ${TARS_FILE} to ${TARS_HEADER_DIR}"
            DEPENDS ${TARS_FILE}
            VERBATIM
        )

        list(APPEND OUT_TARS_H_LIST ${TARS_HEADER_DIR}/${TARS_NAME}.h)
    endforeach()
endif()

set_directory_properties(PROPERTIES ADDITIONAL_MAKE_CLEAN_FILES "${OUT_TARS_H_LIST}")

file(GLOB_RECURSE SRC_LIST bcos-tars-protocol/*.cpp)
file(GLOB_RECURSE CLIENT_SRC bcos-tars-protocol/client/*.cpp)
if (ONLY_CPP_SDK AND (NOT WITH_SWIG_SDK))
    list(REMOVE_ITEM SRC_LIST ${CLIENT_SRC})
    # GroupInfoCodecImpl.cpp implements group-info codec on top of the
    # toBcosGroupInfo/toTarsGroupInfo helpers that Common.h compiles out under
    # ONLY_CPP_SDK (#if !ONLY_CPP_SDK). All of its users (bcos-rpc,
    # bcos-gateway, libinitializer) are full-node components, so drop it from
    # SDK-only builds like the service clients above.
    list(REMOVE_ITEM SRC_LIST
        ${CMAKE_CURRENT_SOURCE_DIR}/bcos-tars-protocol/protocol/GroupInfoCodecImpl.cpp)
endif ()
find_package(tarscpp REQUIRED)
find_package(TBB REQUIRED)
find_package(jsoncpp REQUIRED)

add_library(protocol-tars ${SRC_LIST} ${OUT_TARS_H_LIST})
target_include_directories(protocol-tars PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${TARSCPP_ROOT}/include>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include/bcos-tars-protocol>)
# `codec` carries the RLP headers TransactionImpl.cpp needs for the FIB-New1 canonical Web3
# txHash recompute. release-3.17.0 spelled it ${CODEC_TARGET}; that variable went away with
# TargetSettings.cmake (#5174), so it is named directly here.
# `rlp-protocol` provides EthBlockHeader, which BlockHeaderImpl::calculateHash uses to
# recompute the RLP hash for Ethereum-standard headers.
target_link_libraries(protocol-tars PUBLIC bcos-concepts bcos-framework bcos-crypto codec rlp-protocol tarscpp::tarsservant tarscpp::tarsutil TBB::tbb jsoncpp_static)
set_target_properties(protocol-tars PROPERTIES UNITY_BUILD "ON")

if(TESTS)
    enable_testing()
    set(CTEST_OUTPUT_ON_FAILURE TRUE)
    add_subdirectory(test)
    add_subdirectory(benchmark)
endif()

include(GNUInstallDirs)
install(TARGETS protocol-tars EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "bcos-tars-protocol" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")
install(DIRECTORY "${CMAKE_BINARY_DIR}/generated/bcos-tars-protocol" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")
# Legacy TiKV storage code — moved out of bcos-storage (deprecated).
# Only built when WITH_TIKV is enabled.
if(WITH_TIKV)
    find_package(tikv-client-cpp CONFIG REQUIRED)
    add_library(storage-tikv STATIC TiKVStorage.cpp)
    target_include_directories(storage-tikv PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/bcos-storage)
    target_link_libraries(storage-tikv PUBLIC bcos-framework bcos-utilities kv_client bcos-legacy ittapi)
    set_target_properties(storage-tikv PROPERTIES UNITY_BUILD "ON")
endif()

if(TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

file(GLOB SRC_LIST "*.cpp")

find_package(Boost REQUIRED program_options)

foreach(source ${SRC_LIST})
    get_filename_component(filename ${source} NAME)
    string(REPLACE ".cpp" "" target_name ${filename})
    add_executable(${target_name} ${source})
    target_link_libraries(${target_name} storage security rpc Boost::program_options init ledger pbft_init)
    target_include_directories(${target_name} PUBLIC ${CMAKE_SOURCE_DIR} ../bcos-storage ../../libinitializer)
    target_compile_options(${target_name} PRIVATE -Wno-unused-variable)
endforeach()

if(TESTS)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME storageToolKeyPageRegression
        COMMAND ${CMAKE_COMMAND} -E env
        "STORAGE_TOOL_BIN=$<TARGET_FILE:storageTool>"
        "ASAN_OPTIONS=detect_leaks=0"
        "TMPDIR=${CMAKE_CURRENT_BINARY_DIR}"
        ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/test_storage_tool.py)
    set_tests_properties(storageToolKeyPageRegression PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()

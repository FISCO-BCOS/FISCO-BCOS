file(GLOB SRC_LIST "*.cpp")

find_package(Boost REQUIRED program_options)

foreach(source ${SRC_LIST})
    get_filename_component(filename ${source} NAME)
    string(REPLACE ".cpp" "" target_name ${filename})
    add_executable(${target_name} ${source})
    target_link_libraries(${target_name} ${STORAGE_TARGET} ${SECURITY_TARGET} ${RPC_TARGET} Boost::program_options ${INIT_LIB} ${LEDGER_TARGET} ${PBFT_INIT_LIB})
    target_include_directories(${target_name} PUBLIC ${CMAKE_SOURCE_DIR} ../bcos-storage ../../libinitializer)
    target_compile_options(${target_name} PRIVATE -Wno-unused-variable)
endforeach()

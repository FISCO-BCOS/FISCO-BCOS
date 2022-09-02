find_package(OpenMP 2.0 REQUIRED)
find_package(Boost REQUIRED program_options)

add_executable(merkleBench merkleBench.cpp)
target_link_libraries(merkleBench ${TOOL_TARGET} ${PROTOCOL_TARGET} bcos-crypto Boost::program_options OpenMP::OpenMP_CXX)

file(GLOB sources "*.cpp" "*.h")

add_library(storage ${sources})

target_link_libraries(storage INTERFACE blockverifier)
target_link_libraries(storage PUBLIC devcrypto devcore ${JSONCPP_LIBRARY})

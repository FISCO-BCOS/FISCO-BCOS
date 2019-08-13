file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")
add_library(event ${SRC_LIST} ${HEADERS})
target_link_libraries(event PUBLIC channelserver ledger devcore ethcore JsonCpp)

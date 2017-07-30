aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")

add_library(pailler ${SRC_LIST} ${HEADERS})

install(TARGETS pailler RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib) 
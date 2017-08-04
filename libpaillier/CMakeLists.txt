aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")

add_library(paillier ${SRC_LIST} ${HEADERS})

install(TARGETS paillier RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib) 
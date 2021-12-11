file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_subdirectory(utilities)
add_subdirectory(context)
add_subdirectory(httpserver)
add_subdirectory(websocket)
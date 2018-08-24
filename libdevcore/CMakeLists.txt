file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(devcore ${SRC_LIST} ${HEADERS})

target_include_directories(devcore PRIVATE ..)
target_include_directories(devcore PRIVATE ../utils)
target_include_directories(devcore PUBLIC ${BOOST_INCLUDE_DIR})


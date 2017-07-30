
set(SOURCES
	ExtVMFace.cpp
	VM.cpp
	VMOpt.cpp
	VMCalls.cpp
	VMFactory.cpp
	CoverTool.cpp
	VMExtends.cpp
)

if (EVMJIT)
	list(APPEND SOURCES
		JitVM.cpp
		SmartVM.cpp
	)
endif()

file(GLOB HEADERS "*.h")

add_library(evm ${SOURCES} ${HEADERS})

find_package(Eth)

include_directories(../libodbc/include
					../libodbc/include/db2_linux
					../libodbc/include/ibase
					../libodbc/include/infomix
					../libodbc/include/mysql
					../libodbc/include/odbc_linux
					../libodbc/include/pgsql
					../libodbc/include/sqlbase
					../libodbc/include/sqllite
					../libodbc/include/ss_linux
					../libodbc/include/sybase
					../libpailler)


target_link_libraries(evm ${Eth_ETHCORE_LIBRARIES})
target_link_libraries(evm ${Eth_EVMCORE_LIBRARIES})
target_link_libraries(evm pailler)
target_include_directories(evm PRIVATE .. ${JSONCPP_INCLUDE_DIR})

if (EVMJIT)
	target_include_directories(evm PRIVATE ../evmjit/include)
	target_link_libraries(evm ${Eth_EVMJIT_LIBRARIES})
endif()

install(TARGETS evm RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)

include(ExternalProject)
include(GNUInstallDirs)

if (ENCRYPTTYPE)
	if (CMAKE_SYSTEM_NAME MATCHES "Darwin")
	set(TASSL_CONFIG_COMMAND sh ./Configure darwin64-x86_64-cc)
	else()
		set(TASSL_CONFIG_COMMAND bash config -Wl,--rpath=./ shared)
	endif ()

	set(TASSL_BUILD_COMMAND make)

	ExternalProject_Add(tassl
		PREFIX ${CMAKE_SOURCE_DIR}/deps
		DOWNLOAD_NO_PROGRESS 1
		URL https://github.com/xxxx/TASSL-master.zip
		URL_HASH SHA256=5dd14fcfe070a0c9d3e7d9561502e277e1905a8ce270733bf2884f0e2c0c8d97
		BUILD_IN_SOURCE 1
		CONFIGURE_COMMAND ${TASSL_CONFIG_COMMAND}
		LOG_CONFIGURE 1
		BUILD_COMMAND ${TASSL_BUILD_COMMAND}
		INSTALL_COMMAND ""
	)

	ExternalProject_Get_Property(tassl SOURCE_DIR)
	set(TASSL_SUFFIX .a)
	set(OPENSSL_INCLUDE_DIRS ${SOURCE_DIR}/include)
	set(OPENSSL_SSL_LIBRARIE ${SOURCE_DIR}/libssl${TASSL_SUFFIX})
	set(OPENSSL_CRYPTO_LIBRARIE ${SOURCE_DIR}/libcrypto${TASSL_SUFFIX})
endif()

if (CMAKE_SYSTEM_NAME MATCHES "Darwin")
	set(OPENSSL_INCLUDE_DIRS /usr/local/opt/openssl/include)
    set(OPENSSL_SSL_LIBRARIE /usr/local/opt/openssl/lib/libssl.a)
    set(OPENSSL_CRYPTO_LIBRARIE /usr/local/opt/openssl/lib/libcrypto.a)
endif()
#message(STATUS "###OPENSSL_SSL_LIBRARIE:${OPENSSL_SSL_LIBRARIE}")
#message(STATUS "###OPENSSL_CRYPTO_LIBRARIE:${OPENSSL_CRYPTO_LIBRARIE}")
#message(STATUS "##OPENSSL_INCLUDE_DIRS: ${OPENSSL_INCLUDE_DIRS}")

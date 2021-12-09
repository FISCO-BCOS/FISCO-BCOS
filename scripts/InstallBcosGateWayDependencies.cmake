find_package(bcos-framework CONFIG REQUIRED)

hunter_add_package(OpenSSL)
find_package(OpenSSL REQUIRED)

hunter_add_package(jsoncpp)
find_package(jsoncpp CONFIG REQUIRED)

hunter_add_package(bcos-gateway)
find_package(bcos-gateway CONFIG REQUIRED)

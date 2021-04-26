file(REMOVE_RECURSE
  "../deps/lib/libmysqlclient.a"
  "../deps/src/mysqlclient-stamp/mysqlclient-build"
  "../deps/src/mysqlclient-stamp/mysqlclient-configure"
  "../deps/src/mysqlclient-stamp/mysqlclient-download"
  "../deps/src/mysqlclient-stamp/mysqlclient-install"
  "../deps/src/mysqlclient-stamp/mysqlclient-mkdir"
  "../deps/src/mysqlclient-stamp/mysqlclient-patch"
  "../deps/src/mysqlclient-stamp/mysqlclient-update"
  "CMakeFiles/mysqlclient"
  "CMakeFiles/mysqlclient-complete"
)

# Per-language clean rules from dependency scanning.
foreach(lang )
  include(CMakeFiles/mysqlclient.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()

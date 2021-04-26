file(REMOVE_RECURSE
  "../deps/lib/libtbb.dylib"
  "../deps/src/tbb-stamp/tbb-build"
  "../deps/src/tbb-stamp/tbb-configure"
  "../deps/src/tbb-stamp/tbb-download"
  "../deps/src/tbb-stamp/tbb-install"
  "../deps/src/tbb-stamp/tbb-mkdir"
  "../deps/src/tbb-stamp/tbb-patch"
  "../deps/src/tbb-stamp/tbb-update"
  "CMakeFiles/tbb"
  "CMakeFiles/tbb-complete"
)

# Per-language clean rules from dependency scanning.
foreach(lang )
  include(CMakeFiles/tbb.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()

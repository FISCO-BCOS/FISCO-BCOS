REM call vcvarsall
call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat"

REM add MSBuild to env variable
set MSBuild="c:/Program Files (x86)/MSBuild/14.0/bin/msbuild.exe"

REM add vcvars32.bat to PATH
@set PATH=c:/Program Files (x86)/Microsoft Visual Studio 14.0/VC/bin;%PATH%

REM create build, package && install directory
if not exist build mkdir build
if not exist package mkdir package
if not exist install mkdir install

rem ===========================================================================
rem Layer 0 is Boost.
rem Absolutely everything depends on Boost, whether they use it or not,
rem because of EthDependencies.cmake in webthree-helper, which does an
rem unconditional find_module() for boost, irrespective of what is being built.

rem ===========================================================================
rem Layer 1 are the external libraries.  Do any of these themselves depend on
rem Boost?  I think that the majority or indeed all of them *might not*, and
rem that if we fixed up the CMake code so that the unconditional Boost
rem dependency could be skipped then we could improve the build ordering here.

call :setup x64 Debug     & call build_cryptopp.bat    & call bundle_cryptopp.bat || goto :error
call :setup x64 Release   & call build_cryptopp.bat    & call bundle_cryptopp.bat || goto :error
call install_cryptopp.bat || goto :error

call :setup x64 Debug     & call build_curl.bat        & call bundle_curl.bat || goto :error
call :setup x64 Release   & call build_curl.bat        & call bundle_curl.bat || goto :error
call install_curl.bat || goto :error

rem Is GMP not used for Windows?

call :setup x64 Debug     & call build_jsoncpp.bat     & call bundle_jsoncpp.bat || goto :error
call :setup x64 Release   & call build_jsoncpp.bat     & call bundle_jsoncpp.bat || goto :error
call install_jsoncpp.bat || goto :error

call :setup x64 Debug     & call build_leveldb.bat     & call bundle_leveldb.bat || goto :error
call :setup x64 Release   & call build_leveldb.bat     & call bundle_leveldb.bat || goto :error
call install_leveldb.bat || goto :error

rem libscrypt needs building for crosseth.  Why don't we need it here?

call :setup x64 Debug     & call build_microhttpd.bat  & call bundle_microhttpd.bat || goto :error
call :setup x64 Release   & call build_microhttpd.bat  & call bundle_microhttpd.bat || goto :error
call install_microhttpd.bat || goto :error

rem We don't build libminiupnpc for crosseth, so I'll have to guess on its layering for now.
call :setup x64 Debug     & call build_miniupnpc.bat     & call bundle_miniupnpc.bat || goto :error
call :setup x64 Release   & call build_miniupnpc.bat     & call bundle_miniupnpc.bat || goto :error
call install_miniupnpc.bat || goto :error

rem ===========================================================================
rem Layer 2 comprises secp256k1 and libjson-rpc-cpp (which are external
rem  libraries which depend on Layer 1 external libraries)

call :setup x64 Debug     & call build_jsonrpccpp.bat  & call bundle_jsonrpccpp.bat || goto :error
call :setup x64 Release   & call build_jsonrpccpp.bat  & call bundle_jsonrpccpp.bat || goto :error
call install_jsonrpccpp.bat || goto :error

rem secp256k1 needs building for crosseth.  Why don't we need it here?


goto :EOF

:setup
set PLATFORM=%1
set CONFIGURATION=%2

goto :EOF

:error
echo Failed with error %errorlevel%
exit /b %errorlevel%

:EOF

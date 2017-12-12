REM clone jsoncpp
if not exist build\jsoncpp git clone -q https://github.com/debris/jsoncpp build\jsoncpp
cd build\jsoncpp
git checkout -qf 24c0054c10e62e8359c0f96372dfa183de90f93c

REM create jsoncpp build dirs
if %PLATFORM% == Win32 if not exist build mkdir build
if %PLATFORM% == Win32 cd build
if %PLATFORM% == x64 if not exist build64 mkdir build64
if %PLATFORM% == x64 cd build64

REM run jsoncpp cmake
if %PLATFORM% == Win32 cmake ..
if %PLATFORM% == x64 cmake -G "Visual Studio 14 2015 Win64" ..

REM build jsoncpp
%MSBuild% jsoncpp.sln /property:Configuration=%CONFIGURATION% /property:Platform=%PLATFORM% /target:jsoncpp_lib_static /verbosity:minimal

REM jsoncpp built
cd ..\..\..

REM clone curl
if not exist build\curl git clone -q https://github.com/debris/curl build\curl
cd build\curl
git checkout -qf 139141f8d73eb5820a64b100485572a263f4156b

REM create curl build dirs
if %PLATFORM% == Win32 if not exist build mkdir build
if %PLATFORM% == Win32 cd build
if %PLATFORM% == x64 if not exist build64 mkdir build64
if %PLATFORM% == x64 cd build64

REM run curl cmake
if %PLATFORM% == Win32 cmake ..
if %PLATFORM% == x64 cmake -G "Visual Studio 14 2015 Win64" ..

REM build curl
%MSBuild% CURL.sln /property:Configuration=%CONFIGURATION% /property:Platform=%PLATFORM% /target:libcurl /verbosity:minimal

REM curl built
cd ..\..\..

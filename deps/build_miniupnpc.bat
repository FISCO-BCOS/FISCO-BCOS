REM clone miniupnpc
if not exist build\miniupnpc git clone -q https://github.com/debris/miniupnp build\miniupnpc
cd build\miniupnpc\miniupnpc
git checkout -qf 5459ab79cb5ba8f78e76daf698b70bc76ffef6f1

REM create miniupnpc build dirs
if %PLATFORM% == Win32 if not exist build mkdir build
if %PLATFORM% == Win32 cd build
if %PLATFORM% == x64 if not exist build64 mkdir build64
if %PLATFORM% == x64 cd build64

REM run miniupnpc cmake
if %PLATFORM% == Win32 cmake ..
if %PLATFORM% == x64 cmake -G "Visual Studio 14 2015 Win64" ..

REM build miniupnpc
%MSBuild% miniupnpc.sln /property:Configuration=%CONFIGURATION% /property:Platform=%PLATFORM% /target:upnpc-static /verbosity:minimal

REM miniupnpc built
cd ..\..\..\..

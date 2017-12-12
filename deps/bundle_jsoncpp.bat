REM packaging jsoncpp
if not exist package\jsoncpp mkdir package\jsoncpp

if not exist package\jsoncpp\%PLATFORM% mkdir package\jsoncpp\%PLATFORM%
if not exist package\jsoncpp\%PLATFORM%\lib mkdir package\jsoncpp\%PLATFORM%\lib
if not exist package\jsoncpp\%PLATFORM%\include mkdir package\jsoncpp\%PLATFORM%\include
if not exist package\jsoncpp\%PLATFORM%\include\json mkdir package\jsoncpp\%PLATFORM%\include\json

cd package\jsoncpp\%PLATFORM%

if %PLATFORM% == Win32 (
    if %CONFIGURATION% == Release cmake -E copy ..\..\..\build\jsoncpp\build\src\lib_json\Release\jsoncpp.lib lib\jsoncpp.lib
    if %CONFIGURATION% == Debug cmake -E copy ..\..\..\build\jsoncpp\build\src\lib_json\Debug\jsoncpp.lib lib\jsoncppd.lib
)

if %PLATFORM% == x64 (
    if %CONFIGURATION% == Release cmake -E copy ..\..\..\build\jsoncpp\build64\src\lib_json\Release\jsoncpp.lib lib\jsoncpp.lib
    if %CONFIGURATION% == Debug cmake -E copy ..\..\..\build\jsoncpp\build64\src\lib_json\Debug\jsoncpp.lib lib\jsoncppd.lib
)

cmake -E copy_directory ..\..\..\build\jsoncpp\include\json include\json
cd ..\..\..


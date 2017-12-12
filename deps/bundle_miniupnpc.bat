REM packaging miniupnpc
if not exist package\miniupnpc mkdir package\miniupnpc

if not exist package\miniupnpc\%PLATFORM% mkdir package\miniupnpc\%PLATFORM%
if not exist package\miniupnpc\%PLATFORM%\lib mkdir package\miniupnpc\%PLATFORM%\lib
if not exist package\miniupnpc\%PLATFORM%\include mkdir package\miniupnpc\%PLATFORM%\include
if not exist package\miniupnpc\%PLATFORM%\include\miniupnpc mkdir package\miniupnpc\%PLATFORM%\include\miniupnpc

cd package\miniupnpc\%PLATFORM%

if %PLATFORM% == Win32 (
    if %CONFIGURATION% == Release cmake -E copy ..\..\..\build\miniupnpc\miniupnpc\build\Release\miniupnpc.lib lib\miniupnpc.lib
    if %CONFIGURATION% == Debug cmake -E copy ..\..\..\build\miniupnpc\miniupnpc\build\Debug\miniupnpc.lib lib\miniupnpcd.lib
)

if %PLATFORM% == x64 (
    if %CONFIGURATION% == Release cmake -E copy ..\..\..\build\miniupnpc\miniupnpc\build64\Release\miniupnpc.lib lib\miniupnpc.lib
    if %CONFIGURATION% == Debug cmake -E copy ..\..\..\build\miniupnpc\miniupnpc\build64\Debug\miniupnpc.lib lib\miniupnpcd.lib
)

xcopy ..\..\..\build\miniupnpc\miniupnpc\*.h include\miniupnpc /sy

cd ..\..\..


REM packaging jsonrpccpp
if not exist package\jsonrpccpp mkdir package\jsonrpccpp
if not exist package\jsonrpccpp\%PLATFORM% mkdir package\jsonrpccpp\%PLATFORM%
if not exist package\jsonrpccpp\%PLATFORM%\lib mkdir package\jsonrpccpp\%PLATFORM%\lib
if not exist package\jsonrpccpp\%PLATFORM%\include mkdir package\jsonrpccpp\%PLATFORM%\include
if not exist package\jsonrpccpp\%PLATFORM%\include\jsonrpccpp mkdir package\jsonrpccpp\%PLATFORM%\include\jsonrpccpp

cd package\jsonrpccpp\%PLATFORM%

if %PLATFORM% == Win32 (
    if %CONFIGURATION% == Release (
        cmake -E copy ..\..\..\build\jsonrpccpp\build\lib\Release\jsonrpccpp-common.lib lib\jsonrpccpp-common.lib
        cmake -E copy ..\..\..\build\jsonrpccpp\build\lib\Release\jsonrpccpp-client.lib lib\jsonrpccpp-client.lib
        cmake -E copy ..\..\..\build\jsonrpccpp\build\lib\Release\jsonrpccpp-server.lib lib\jsonrpccpp-server.lib
    )
    
    if %CONFIGURATION% == Debug (
        cmake -E copy ..\..\..\build\jsonrpccpp\build\lib\Debug\jsonrpccpp-common.lib lib\jsonrpccpp-commond.lib
        cmake -E copy ..\..\..\build\jsonrpccpp\build\lib\Debug\jsonrpccpp-client.lib lib\jsonrpccpp-clientd.lib
        cmake -E copy ..\..\..\build\jsonrpccpp\build\lib\Debug\jsonrpccpp-server.lib lib\jsonrpccpp-serverd.lib
    )
    
    xcopy ..\..\..\build\jsonrpccpp\build\gen\jsonrpccpp\*.h include\jsonrpccpp /sy
)

if %PLATFORM% == x64 (
    if %CONFIGURATION% == Release (
        cmake -E copy ..\..\..\build\jsonrpccpp\build64\lib\Release\jsonrpccpp-common.lib lib\jsonrpccpp-common.lib
        cmake -E copy ..\..\..\build\jsonrpccpp\build64\lib\Release\jsonrpccpp-client.lib lib\jsonrpccpp-client.lib
        cmake -E copy ..\..\..\build\jsonrpccpp\build64\lib\Release\jsonrpccpp-server.lib lib\jsonrpccpp-server.lib
    )
    
    if %CONFIGURATION% == Debug (
        cmake -E copy ..\..\..\build\jsonrpccpp\build64\lib\Debug\jsonrpccpp-common.lib lib\jsonrpccpp-commond.lib
        cmake -E copy ..\..\..\build\jsonrpccpp\build64\lib\Debug\jsonrpccpp-client.lib lib\jsonrpccpp-clientd.lib
        cmake -E copy ..\..\..\build\jsonrpccpp\build64\lib\Debug\jsonrpccpp-server.lib lib\jsonrpccpp-serverd.lib
    )

    xcopy ..\..\..\build\jsonrpccpp\build64\gen\jsonrpccpp\*.h include\jsonrpccpp /sy
)

xcopy ..\..\..\build\jsonrpccpp\src\jsonrpccpp\*.h include\jsonrpccpp /sy

cd ..\..\..


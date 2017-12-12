REM packaging microhttpd
if not exist package\microhttpd mkdir package\microhttpd
if not exist package\microhttpd\%PLATFORM% mkdir package\microhttpd\%PLATFORM%
if not exist package\microhttpd\%PLATFORM%\lib mkdir package\microhttpd\%PLATFORM%\lib
if not exist package\microhttpd\%PLATFORM%\bin mkdir package\microhttpd\%PLATFORM%\bin
if not exist package\microhttpd\%PLATFORM%\include mkdir package\microhttpd\%PLATFORM%\include

cd package\microhttpd\%PLATFORM%

if %PLATFORM% == Win32 (
    if %CONFIGURATION% == Release (
        cmake -E copy ..\..\..\build\microhttpd\w32\VS2015\Output\libmicrohttpd-dll.dll bin\libmicrohttpd-dll.dll
        cmake -E copy ..\..\..\build\microhttpd\w32\VS2015\Output\libmicrohttpd-dll.exp lib\libmicrohttpd-dll.exp
        cmake -E copy ..\..\..\build\microhttpd\w32\VS2015\Output\libmicrohttpd-dll.lib lib\libmicrohttpd-dll.lib
        cmake -E copy ..\..\..\build\microhttpd\w32\VS2015\Output\libmicrohttpd-dll.pdb lib\libmicrohttpd-dll.pdb
    )

    if %CONFIGURATION% == Debug (
        cmake -E copy ..\..\..\build\microhttpd\w32\VS2015\Output\libmicrohttpd-dll_d.dll bin\libmicrohttpd-dll_d.dll
        cmake -E copy ..\..\..\build\microhttpd\w32\VS2015\Output\libmicrohttpd-dll_d.exp lib\libmicrohttpd-dll_d.exp
        cmake -E copy ..\..\..\build\microhttpd\w32\VS2015\Output\libmicrohttpd-dll_d.ilk lib\libmicrohttpd-dll_d.ilk
        cmake -E copy ..\..\..\build\microhttpd\w32\VS2015\Output\libmicrohttpd-dll_d.lib lib\libmicrohttpd-dll_d.lib
        cmake -E copy ..\..\..\build\microhttpd\w32\VS2015\Output\libmicrohttpd-dll_d.pdb lib\libmicrohttpd-dll_d.pdb
    )

    cmake -E copy ..\..\..\build\microhttpd\w32\VS2015\Output\microhttpd.h include\microhttpd.h
)

if %PLATFORM% == x64 (
    if %CONFIGURATION% == Release (
        cmake -E copy ..\..\..\build\microhttpd\w32\VS2015\Output\x64\libmicrohttpd-dll.dll bin\libmicrohttpd-dll.dll
        cmake -E copy ..\..\..\build\microhttpd\w32\VS2015\Output\x64\libmicrohttpd-dll.exp lib\libmicrohttpd-dll.exp
        cmake -E copy ..\..\..\build\microhttpd\w32\VS2015\Output\x64\libmicrohttpd-dll.lib lib\libmicrohttpd-dll.lib
        cmake -E copy ..\..\..\build\microhttpd\w32\VS2015\Output\x64\libmicrohttpd-dll.pdb lib\libmicrohttpd-dll.pdb
    )

    if %CONFIGURATION% == Debug (
        cmake -E copy ..\..\..\build\microhttpd\w32\VS2015\Output\x64\libmicrohttpd-dll_d.dll bin\libmicrohttpd-dll_d.dll
        cmake -E copy ..\..\..\build\microhttpd\w32\VS2015\Output\x64\libmicrohttpd-dll_d.exp lib\libmicrohttpd-dll_d.exp
        cmake -E copy ..\..\..\build\microhttpd\w32\VS2015\Output\x64\libmicrohttpd-dll_d.ilk lib\libmicrohttpd-dll_d.ilk
        cmake -E copy ..\..\..\build\microhttpd\w32\VS2015\Output\x64\libmicrohttpd-dll_d.lib lib\libmicrohttpd-dll_d.lib
        cmake -E copy ..\..\..\build\microhttpd\w32\VS2015\Output\x64\libmicrohttpd-dll_d.pdb lib\libmicrohttpd-dll_d.pdb
    )

    cmake -E copy ..\..\..\build\microhttpd\w32\VS2015\Output\x64\microhttpd.h include\microhttpd.h
)

cd ..\..\..

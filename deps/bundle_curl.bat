REM packaging curl
if not exist package\curl mkdir package\curl

if not exist package\curl\%PLATFORM% mkdir package\curl\%PLATFORM%
if not exist package\curl\%PLATFORM%\lib mkdir package\curl\%PLATFORM%\lib
if not exist package\curl\%PLATFORM%\bin mkdir package\curl\%PLATFORM%\bin
if not exist package\curl\%PLATFORM%\include mkdir package\curl\%PLATFORM%\include
if not exist package\curl\%PLATFORM%\include\curl mkdir package\curl\%PLATFORM%\include\curl

cd package\curl\%PLATFORM%

if %PLATFORM% == Win32 (
    if %CONFIGURATION% == Release (
        cmake -E copy ..\..\..\build\curl\build\lib\Release\libcurl.dll bin\libcurl.dll
        cmake -E copy ..\..\..\build\curl\build\lib\Release\libcurl_imp.exp lib\libcurl.exp
        cmake -E copy ..\..\..\build\curl\build\lib\Release\libcurl_imp.lib lib\libcurl.lib
    )

    if %CONFIGURATION% == Debug (
        cmake -E copy ..\..\..\build\curl\build\lib\Debug\libcurl.dll bin\libcurld.dll
        cmake -E copy ..\..\..\build\curl\build\lib\Debug\libcurl.ilk lib\libcurld.ilk
        cmake -E copy ..\..\..\build\curl\build\lib\Debug\libcurl.pdb lib\libcurld.pdb
        cmake -E copy ..\..\..\build\curl\build\lib\Debug\libcurl_imp.exp lib\libcurld.exp
        cmake -E copy ..\..\..\build\curl\build\lib\Debug\libcurl_imp.lib lib\libcurld.lib
    )

    cmake -E copy ..\..\..\build\curl\build\include\curl\curlbuild.h include\curl\curlbuild.h
)

if %PLATFORM% == x64 (
    if %CONFIGURATION% == Release (
        cmake -E copy ..\..\..\build\curl\build64\lib\Release\libcurl.dll bin\libcurl.dll
        cmake -E copy ..\..\..\build\curl\build64\lib\Release\libcurl_imp.exp lib\libcurl.exp
        cmake -E copy ..\..\..\build\curl\build64\lib\Release\libcurl_imp.lib lib\libcurl.lib
    )

    if %CONFIGURATION% == Debug (
        cmake -E copy ..\..\..\build\curl\build64\lib\Debug\libcurl.dll bin\libcurld.dll
        cmake -E copy ..\..\..\build\curl\build64\lib\Debug\libcurl.ilk lib\libcurld.ilk
        cmake -E copy ..\..\..\build\curl\build64\lib\Debug\libcurl.pdb lib\libcurld.pdb
        cmake -E copy ..\..\..\build\curl\build64\lib\Debug\libcurl_imp.exp lib\libcurld.exp
        cmake -E copy ..\..\..\build\curl\build64\lib\Debug\libcurl_imp.lib lib\libcurld.lib
    )

    cmake -E copy ..\..\..\build\curl\build64\include\curl\curlbuild.h include\curl\curlbuild.h
)

cmake -E copy_directory ..\..\..\build\curl\include\curl include\curl

cd ..\..\..


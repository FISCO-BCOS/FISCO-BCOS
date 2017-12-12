REM packaging cryptopp
if not exist package\cryptopp mkdir package\cryptopp

if not exist package\cryptopp\%PLATFORM% mkdir package\cryptopp\%PLATFORM%
if not exist package\cryptopp\%PLATFORM%\lib mkdir package\cryptopp\%PLATFORM%\lib
if not exist package\cryptopp\%PLATFORM%\include mkdir package\cryptopp\%PLATFORM%\include
if not exist package\cryptopp\%PLATFORM%\include\cryptopp mkdir package\cryptopp\%PLATFORM%\include\cryptopp

cd package\cryptopp\%PLATFORM%

if %PLATFORM% == Win32 (
    if %CONFIGURATION% == Release cmake -E copy ..\..\..\build\cryptopp\win\Release\LibCryptoPP.lib lib\cryptopp.lib
    if %CONFIGURATION% == Debug cmake -E copy ..\..\..\build\cryptopp\win\Debug\LibCryptoPP.lib lib\cryptoppd.lib
)

if %PLATFORM% == x64 (
    if %CONFIGURATION% == Release cmake -E copy ..\..\..\build\cryptopp\win\x64\Release\LibCryptoPP.lib lib\cryptopp.lib
    if %CONFIGURATION% == Debug cmake -E copy ..\..\..\build\cryptopp\win\x64\Debug\LibCryptoPP.lib lib\cryptoppd.lib
)

xcopy ..\..\..\build\cryptopp\*.h include\cryptopp /sy
cd ..\..\..


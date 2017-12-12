REM clone microhttpd
if not exist build\microhttpd git clone -q https://github.com/svn2github/libmicrohttpd build\microhttpd
cd build\microhttpd\w32\VS2015
git checkout -qf 82ee737a39be28a32048248655d780ad8a240939

REM build microhttpd
%MSBuild% libmicrohttpd.sln /property:Configuration=%CONFIGURATION%-dll /property:Platform=%PLATFORM% /target:libmicrohttpd /verbosity:minimal

REM microhttpd built
cd ..\..\..\..

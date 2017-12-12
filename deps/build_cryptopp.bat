REM clone cryptopp
if not exist build\cryptopp git clone -q https://github.com/ethereum/cryptopp build\cryptopp
cd build\cryptopp\win
git checkout -qf eb2efc3eaec9a178c0f2049894417ca8c0b8bad4

REM build cryptopp
%MSBuild% LibCryptoPP.sln /property:Configuration=%CONFIGURATION% /property:Platform=%PLATFORM% /verbosity:minimal

REM cryptopp built
cd ..\..\..

call %~dp0\getvars.bat
cd %freeRdpDir%\..\OpenSSL
git clean -xdff

pushd .
call "%vsDir%\VC\Auxiliary\Build\vcvars64.bat"
popd

perl Configure VC-WIN64A no-asm no-shared --prefix=%~dp0\..\..\..\OpenSSL-VC-64
nmake
nmake install

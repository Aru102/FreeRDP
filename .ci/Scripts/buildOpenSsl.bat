call %~dp0\getvars.bat
pushd .
cd %freeRdpDir%\..\OpenSSL
git clean -xdff


perl Configure VC-WIN64A no-asm --prefix=%~dp0\..\..\..\OpenSSL-VC-64
call ms\do_win64a
nmake -f ms\nt.mak
nmake -f ms\nt.mak install

popd

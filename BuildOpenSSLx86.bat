set "START_DIR=%CD%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2017\Professional\VC\Auxiliary\Build\vcvars32.bat"
cd /D "%START_DIR%"

perl Configure VC-WIN32 no-asm --prefix=..\OpenSSL-VC-32
call ms\do_ms
nmake -f ms\nt.mak
nmake -f ms\nt.mak install

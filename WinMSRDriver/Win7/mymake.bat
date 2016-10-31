set BUILD_PATH=

rmdir /S /Q objfre_win7_amd64
nmake

rem chdir objfre_win7_amd64\amd64
rem copy msr.sys c:\
rem chdir ..
rem chdir ..
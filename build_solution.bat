
REM change path to your VCVARS.BAT
CALL "c:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\bin\x86_amd64\vcvarsx86_amd64.bat"
SET "PATH=C:\Program Files (x86)\MSBuild\14.0\Bin\amd64;%PATH%"
REM CALL "c:\Program Files (x86)\Microsoft Visual Studio\2017\Professional\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
REM SET "PATH=C:\Program Files (x86)\Microsoft Visual Studio\2017\Professional\MSBuild\15.0\Bin\amd64;%PATH%"

   msbuild  pcm-all.sln /p:Configuration=Release;Platform=x64  /t:Clean,Build /m 

exit



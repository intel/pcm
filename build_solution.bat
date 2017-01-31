
REM change path to your VCVARS.BAT
CALL "c:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\bin\x86_amd64\vcvarsx86_amd64.bat"
SET "PATH=C:\Program Files (x86)\MSBuild\14.0\Bin\amd64;%PATH%"

   msbuild  pcm-all.sln /p:Configuration=Release  /t:Clean,Build /m 

exit



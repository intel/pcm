
REM change path to your VCVARS.BAT
CALL "c:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\bin\x86_amd64\vcvarsx86_amd64.bat"
SET "PATH=C:\Program Files (x86)\MSBuild\14.0\Bin\amd64;%PATH%"

for %%p in (PCM) do (
   @echo Building %%p
   chdir %%p_Win
   devenv %%p.vcxproj /upgrade
   msbuild  %%p.vcxproj /p:Configuration=Release  /t:Clean,Build
   chdir ..
)

   @echo Building Intelpcm.dll
   chdir Intelpcm.dll
   devenv Intelpcm.dll.vcxproj /upgrade
   msbuild  Intelpcm.dll.vcxproj /p:Configuration=Release  /t:Clean,Build
   chdir ..

   @echo Building PCM-Service 
   chdir PCM-Service_Win
   devenv PCMService.vcxproj /upgrade
   msbuild  PCMService.vcxproj /p:Configuration=Release  /t:Clean,Build
   chdir ..


for %%p in (PCM-MSR PCM-TSX PCM-Memory PCM-NUMA PCM-PCIE PCM-Power PCM-Core) do (
   @echo Building %%p
   chdir %%p_Win
   devenv %%p-win.vcxproj /upgrade
   msbuild  %%p-win.vcxproj /p:Configuration=Release  /t:Clean,Build
   chdir ..
)

exit



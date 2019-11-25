**COMPILATION AND INSTALLATION OF UTILITIES**

_For support of systems with more than _**_64_**_ logical cores you need to compile all binaries below in “_**_x64_**_” platform mode (not the default “_**_Win32_**_” mode)._

Command-line utility:

1.Compile the windows MSR driver (msr.sys) with Windows DDK Kit (see the sources in the WinMSRDriver directory). For Windows 7 you have to sign the msr.sys driver additionally ([http://msdn.microsoft.com/en-us/library/ms537361(VS.85).aspx](%22http://)).

2.Build the pcm.exe utility in the PCM_Win using Microsoft Visual Studio

3.Copy the msr.sys driver and pcm.exe into a single directory

4.Run pcm.exe utility from this directory

For Windows 7 and Windows Server 2008 R2 the PCM utilities need to be run as administrator:

Alternatively you can achieve the same using the “Properties” Windows menu of the executable (“Privilege level” setting in the “Compatibilty” tab): Right mouse click -&gt; Properties -&gt; Compatibility -&gt; Privilege level -&gt; Set “Run this program as an administrator”.

![Screenshot](run-as-administrator.png)

Graphical Perfmon front end:

1.Compile the windows MSR driver (msr.sys) with Windows DDK Kit (see the sources in the WinMSRDriver directory). For Windows 7 you have to sign the msr.sys driver additionally ([http://msdn.microsoft.com/en-us/library/ms537361(VS.85).aspx](http://msdn.microsoft.com/en-us/library/ms537361(VS.85).aspx)).

2.Copy msr.sys into the c:\windows\system32 directory

3.Build pcm-lib.dll in the PCM-Lib_Win directory using Microsoft Visual Studio

4.Build 'PCM-Service.exe' in the PCM-Service_Win directory using Microsoft Visual Studio

5.Copy PCM-Service.exe, PCM-Service.exe.config, and pcm-lib.dll files into a single directory

The config file enables support for legacy security policy. Without this configuration switch, you will get an exception like this:

Unhandled Exception: System.NotSupportedException: This method implicitly uses CAS policy, which has been obsoleted by the .NET Framework.   

6.With administrator rights execute '"PCM-Service.exe" -Install' from this directory

7.With administrator rights execute 'net start pcmservice'

8.Start perfmon and find new PCM\* counters

If you do not want or cannot compile the msr.sys driver you might use a third-party open source WinRing0 driver instead. Instructions:

1. Download the free RealTemp utility package from [http://www.techpowerup.com/realtemp/](http://www.techpowerup.com/realtemp/) or any other free utility that uses the open-source WinRing0 driver (like OpenHardwareMonitor [http://code.google.com/p/open-hardware-monitor/downloads/list](http://code.google.com/p/open-hardware-monitor/downloads/list)).
2. Copy WinRing0.dll, WinRing0.sys, WinRing0x64.dll, WinRing0x64.sys files from there into the PCM.exe binary location, into the PCM-Service.exe location and into c:\windows\system32
3. Run the PCM.exe tool and/or go to step 6 (perfmon utility).

Known limitations:

Running PCM.exe under Cygwin shell is possible, but due to incompatibilities of signals/events handling between Windows and Cygwin, the PCM may not cleanup PMU configuration after Ctrl+C or Ctrl+Break pressed. The subsequent run of PCM will require to do PMU configuration reset, so adding -r command line option to PCM will be required.

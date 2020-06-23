#ifndef WINDRIVER_HEADER
#define WINDRIVER_HEADER

/*
Copyright (c) 2009-2012, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
// contact: Roman Dementiev
// WARNING: This driver code is only for testing purposes, not for production use
//

#include <iostream>
#include <winreg.h>
#include <comdef.h>
#include "cpucounters.h"

namespace pcm {

/*!     \file windriver.h
        \brief Loading and unloading custom Windows MSR (Model Specific Register) Driver
*/

extern void restrictDriverAccess(LPCWSTR path);

/*! \brief Manage custom Windows MSR (Model Specific Register) Driver
    The driver is required to access hardware Model Specific Registers (MSRs)
    under Windows. Currently only 64-bit Windows 7 has been tested.
*/

class Driver
{
    SC_HANDLE hSCManager;
    SC_HANDLE hService;
    SERVICE_STATUS ss;

public:
    static std::wstring msrLocalPath()
    {
        std::wstring driverPath;
        DWORD driverPathLen = 1;
        DWORD gcdReturn = 0;

        do {
            if (0 != gcdReturn)
            {
                driverPathLen = gcdReturn;
            }
            driverPath.resize(driverPathLen);
            gcdReturn = GetCurrentDirectory(driverPathLen, &driverPath[0]);
        } while (0 != gcdReturn && driverPathLen < gcdReturn);

        removeNullTerminator(driverPath);

        return driverPath + L"\\msr.sys";
    }

    Driver() :
        Driver(L"c:\\windows\\system32\\msr.sys")
    {
    }

    Driver(const std::wstring& driverPath) :
        Driver(driverPath, L"PCM Test MSR", L"PCM Test MSR Driver")
    {
    }

    Driver(const std::wstring& driverPath, const std::wstring& driverName, const std::wstring& driverDescription) :
        driverPath_(setConfigValue(L"DriverPath", driverPath)),
        driverName_(setConfigValue(L"DriverName", driverName)),
        driverDescription_(setConfigValue(L"DriverDescription", driverDescription))
    {
    }

    const std::wstring& driverPath() const
    {
        return driverPath_;
    }


    /*! \brief Installs and loads the driver

        Installs the driver if not installed and then loads it.

        \param driverPath full path to the driver
        \return true iff driver start up was successful
    */
    bool start()
    {
        hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
        if (hSCManager)
        {
            hService = CreateService(hSCManager, &driverName_[0], &driverDescription_[0], SERVICE_START | DELETE | SERVICE_STOP,
                                     SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START, SERVICE_ERROR_IGNORE, &driverPath_[0], NULL, NULL, NULL, NULL, NULL);

            if (!hService)
            {
                hService = OpenService(hSCManager, &driverName_[0], SERVICE_START | DELETE | SERVICE_STOP);
            }

            if (hService)
            {
                if (0 != StartService(hService, 0, NULL))
                {
                    std::wstring convDriverName(&driverName_[0]);
                    std::wstring driverPath = L"\\\\.\\" + convDriverName;
                    restrictDriverAccess(driverPath.c_str());
                    return true;
                }
                DWORD err = GetLastError();
                if (err == ERROR_SERVICE_ALREADY_RUNNING) return true;

                std::wcerr << "Starting MSR service failed with error " << err << " ";
                const TCHAR * errorStr = _com_error(err).ErrorMessage();
                if (errorStr)
                    std::wcerr << errorStr << "\n";

                ControlService(hService, SERVICE_CONTROL_STOP, &ss);

                // DeleteService(hService);

                CloseServiceHandle(hService);
            }
            else
            {
                std::wcerr << "Opening service manager failed with error " << GetLastError() << " ";
                const TCHAR * errorStr = _com_error(GetLastError()).ErrorMessage();
                if (errorStr)
                    std::wcerr << errorStr << "\n";
            }

            CloseServiceHandle(hSCManager);
        }
        else
        {
            std::wcerr << "Opening service manager failed with error " << GetLastError() << " ";
            const TCHAR * errorStr = _com_error(GetLastError()).ErrorMessage();
            if (errorStr)
                std::wcerr << errorStr << "\n";
        }

        #ifndef NO_WINRING // In cases where loading the WinRing0 driver is not desirable as a fallback to MSR.sys, add -DNO_WINRING to compile command to remove ability to load driver (will also remove initWinRing0Lib function)
        std::cerr << "Trying to load winring0.dll/winring0.sys driver...\n";
        if(PCM::initWinRing0Lib())
        {
            std::cerr << "Using winring0.dll/winring0.sys driver.\n\n";
            return true;
        }
        else
        {
            std::cerr << "Failed to load winring0.dll/winring0.sys driver.\n\n";
        }
        #endif // NO_WINRING

        return false;
    }

    //! \brief Stop and unload the driver
    void stop()
    {
        hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
        if (hSCManager)
        {
            hService = OpenService(hSCManager, &driverName_[0], SERVICE_START | DELETE | SERVICE_STOP);
            if (hService)
            {
                ControlService(hService, SERVICE_CONTROL_STOP, &ss);
                CloseServiceHandle(hService);
            }

            CloseServiceHandle(hSCManager);
        }
        else
        {
            std::wcerr << "Opening service manager failed with error " << GetLastError() << " ";
            const TCHAR * errorStr = _com_error(GetLastError()).ErrorMessage();
            if (errorStr)
                std::wcerr << errorStr;
        }
    }

    /*! \brief Uninstall the driver

         Uninstalls the driver. For successeful uninstallation you need to reboot the system after calling this method.
    */
    void uninstall()
    {
        hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
        if (hSCManager)
        {
            hService = OpenService(hSCManager, &driverName_[0], SERVICE_START | DELETE | SERVICE_STOP);
            if (hService)
            {
                ControlService(hService, SERVICE_CONTROL_STOP, &ss);
                DeleteService(hService);
                CloseServiceHandle(hService);
            }

            CloseServiceHandle(hSCManager);
        }
        else
        {
            std::wcerr << "Opening service manager failed with error " << GetLastError() << " ";
            const TCHAR * errorStr = _com_error(GetLastError()).ErrorMessage();
            if (errorStr)
                std::wcerr << errorStr;
        }
    }

private:

    static std::wstring setConfigValue(const LPCWSTR key, const std::wstring& defaultValue)
    {
        static_assert(std::is_same<WCHAR, wchar_t>::value, "WCHAR expected to be wchar_t");

        std::wstring regRead;
        DWORD regLen = 1 * sizeof(WCHAR);
        DWORD regRes = ERROR_FILE_NOT_FOUND; // Safe error to start with in case key doesn't exist

        HKEY hKey;
        if (ERROR_SUCCESS == RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"SOFTWARE\\pcm", NULL, KEY_READ, &hKey))
        {
            do {
                regRead.resize(regLen / sizeof(WCHAR));
                regRes = RegQueryValueEx(hKey, key, NULL, NULL, (LPBYTE)&regRead[0], &regLen);
            } while (ERROR_MORE_DATA == regRes);

            RegCloseKey(hKey);
        }

        removeNullTerminator(regRead);
            
        return ERROR_SUCCESS == regRes ? regRead : defaultValue;
    }

    static void removeNullTerminator(std::wstring& s)
    {
        if (!s.empty() && s.back() == '\0')
        {
            s.pop_back();
        }
    }

    const std::wstring driverName_;
    const std::wstring driverPath_;
    const std::wstring driverDescription_;
};

} // namespace pcm

#endif

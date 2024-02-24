// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation

#ifndef WINDRIVER_HEADER
#define WINDRIVER_HEADER

// contact: Roman Dementiev
// WARNING: This driver code is only for testing purposes, not for production use
//

#include <iostream>
#include <winreg.h>
#include <comdef.h>
#include "../cpucounters.h"

namespace pcm {

/*!     \file windriver.h
        \brief Loading and unloading custom Windows MSR (Model Specific Register) Driver
*/

extern void restrictDriverAccess(LPCTSTR path);

/*! \brief Manage custom Windows MSR (Model Specific Register) Driver
    The driver is required to access hardware Model Specific Registers (MSRs)
    under Windows. Currently only 64-bit Windows 7 has been tested.
*/

class Driver
{
    SC_HANDLE hSCManager{};
    SC_HANDLE hService{};
    SERVICE_STATUS ss{};

public:
    static tstring msrLocalPath()
    {
        tstring driverPath;
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

        return driverPath + TEXT("\\msr.sys");
    }

    Driver() :
        Driver(TEXT("c:\\windows\\system32\\msr.sys"))
    {
    }

    Driver(const tstring& driverPath) :
        Driver(driverPath, TEXT("PCM MSR"), TEXT("PCM MSR Driver"))
    {
    }

    Driver(const tstring& driverPath, const tstring& driverName, const tstring& driverDescription) :
        driverPath_(setConfigValue(TEXT("DriverPath"), driverPath)),
        driverName_(setConfigValue(TEXT("DriverName"), driverName)),
        driverDescription_(setConfigValue(TEXT("DriverDescription"), driverDescription))
    {
    }

    const tstring& driverPath() const
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
                    restrictDriverAccess(PCM_MSR_DRV_NAME);
                    return true;
                }
                DWORD err = GetLastError();
                if (err == ERROR_SERVICE_ALREADY_RUNNING) return true;

                std::wcerr << "Starting MSR service failed with error " << err << " ";
                const _com_error comError{ (int)err };
                const TCHAR * errorStr = comError.ErrorMessage();
                if (errorStr)
                    std::wcerr << errorStr << "\n";

                ControlService(hService, SERVICE_CONTROL_STOP, &ss);

                // DeleteService(hService);

                CloseServiceHandle(hService);
            }
            else
            {
                std::wcerr << "Opening service manager failed with error " << GetLastError() << " ";
                const _com_error comError{ (int)GetLastError() };
                const TCHAR * errorStr = comError.ErrorMessage();
                if (errorStr)
                    std::wcerr << errorStr << "\n";
            }

            CloseServiceHandle(hSCManager);
        }
        else
        {
            std::wcerr << "Opening service manager failed with error " << GetLastError() << " ";
            const _com_error comError{ (int)GetLastError() };
            const TCHAR * errorStr = comError.ErrorMessage();
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
            const _com_error comError{ (int)GetLastError() };
            const TCHAR * errorStr = comError.ErrorMessage();
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
            const _com_error comError{ (int)GetLastError() };
            const TCHAR * errorStr = comError.ErrorMessage();
            if (errorStr)
                std::wcerr << errorStr;
        }
    }

private:

    static tstring setConfigValue(LPCTSTR key, const tstring& defaultValue)
    {
        tstring regRead;
        DWORD regLen = 1 * sizeof(TCHAR);
        DWORD regRes = ERROR_FILE_NOT_FOUND; // Safe error to start with in case key doesn't exist

        HKEY hKey;
        if (ERROR_SUCCESS == RegOpenKeyEx(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\pcm"), NULL, KEY_READ, &hKey))
        {
            do {
                regRead.resize(regLen / sizeof(TCHAR));
                regRes = RegQueryValueEx(hKey, key, NULL, NULL, (LPBYTE)&regRead[0], &regLen);
            } while (ERROR_MORE_DATA == regRes);

            RegCloseKey(hKey);
        }

        removeNullTerminator(regRead);
            
        return ERROR_SUCCESS == regRes ? regRead : defaultValue;
    }

    static void removeNullTerminator(tstring& s)
    {
        if (!s.empty() && s.back() == '\0')
        {
            s.pop_back();
        }
    }

    const tstring driverName_;
    const tstring driverPath_;
    const tstring driverDescription_;
};

} // namespace pcm

#endif

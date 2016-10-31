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
#include <comdef.h>
#include "cpucounters.h"

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
    /*! \brief Installs and loads the driver

        Installs the driver if not installed and then loads it.

        \param driverPath full path to the driver
        \return true iff driver start up was successful
    */
    bool start(LPCWSTR driverPath)
    {
        hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
        if (hSCManager)
        {
            hService = CreateService(hSCManager, L"PCM Test MSR", L"PCM Test MSR Driver", SERVICE_START | DELETE | SERVICE_STOP,
                                     SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START, SERVICE_ERROR_IGNORE, driverPath, NULL, NULL, NULL, NULL, NULL);

            if (!hService)
            {
                hService = OpenService(hSCManager, L"PCM Test MSR", SERVICE_START | DELETE | SERVICE_STOP);
            }

            if (hService)
            {
                if (0 != StartService(hService, 0, NULL))
                {
                    restrictDriverAccess(L"\\\\.\\PCM Test MSR");
                    return true;
                }
                DWORD err = GetLastError();
                if (err == ERROR_SERVICE_ALREADY_RUNNING) return true;

                std::wcerr << "Starting MSR service failed with error " << err << " ";
                const TCHAR * errorStr = _com_error(err).ErrorMessage();
                if (errorStr) std::wcerr << errorStr;
                std::wcerr << std::endl;

                ControlService(hService, SERVICE_CONTROL_STOP, &ss);

                // DeleteService(hService);

                CloseServiceHandle(hService);
            }
            else
            {
                std::wcerr << "Opening service manager failed with error " << GetLastError() << " ";
                const TCHAR * errorStr = _com_error(GetLastError()).ErrorMessage();
                if (errorStr) std::wcerr << errorStr;
                std::wcerr << std::endl;
            }

            CloseServiceHandle(hSCManager);
        }
        else
        {
            std::wcerr << "Opening service manager failed with error " << GetLastError() << " ";
            const TCHAR * errorStr = _com_error(GetLastError()).ErrorMessage();
            if (errorStr) std::wcerr << errorStr;
            std::wcerr << std::endl;
        }


		std::cerr << "Trying to load winring0.dll/winring0.sys driver..." << std::endl;
		if(PCM::initWinRing0Lib())
		{
			std::cerr << "Using winring0.dll/winring0.sys driver.\n" << std::endl;
			return true;
		}
		else
		{
			std::cerr << "Failed to load winring0.dll/winring0.sys driver.\n" << std::endl;
		}

        return false;
    }

    //! \brief Stop and unload the driver
    void stop()
    {
        hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
        if (hSCManager)
        {
            hService = OpenService(hSCManager, L"PCM Test MSR", SERVICE_START | DELETE | SERVICE_STOP);
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
            if (errorStr) std::wcerr << errorStr;
            std::wcerr << std::endl;
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
            hService = OpenService(hSCManager, L"PCM Test MSR", SERVICE_START | DELETE | SERVICE_STOP);
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
            if (errorStr) std::wcerr << errorStr;
            std::wcerr << std::endl;
        }
    }
};

#endif

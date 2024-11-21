// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation

/*
  Copyright 2012 Michael Cohen <scudette@gmail.com>

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

/********************************************************************
   This is a single binary memory imager for Windows.

   Supported systems:
    - Windows XPSP2 to Windows 8 inclusive, both 32 bit and 64 bit.

*********************************************************************/

#include "winpmem.h"

#ifdef PCM_EXPORTS
#define PCM_API __declspec(dllexport)
#else
#define PCM_API 
#endif

namespace pcm {

extern PCM_API void restrictDriverAccess(LPCTSTR path);

int WinPmem::set_acquisition_mode(__int32 mode) {
  DWORD size;
  // Set the acquisition mode.
  if(!DeviceIoControl(fd_, PMEM_CTRL_IOCTRL, &mode, 4, NULL, 0,
                      &size, NULL)) {
    LogError(TEXT("Failed to set acquisition mode.\n"));
    return -1;
  };

  return 1;
};

int WinPmem::toggle_write_mode() {
    DWORD size;
    // Set the acquisition mode.
    if (!DeviceIoControl(fd_, PMEM_WRITE_ENABLE, NULL, 0, NULL, 0,
        &size, NULL)) {
        LogError(TEXT("INFO: winpmem driver does not support write mode.\n"));
        return -1;
    };

    return 1;
};

WinPmem::WinPmem():
  suppress_output(FALSE),
  fd_(INVALID_HANDLE_VALUE),
  out_fd_(INVALID_HANDLE_VALUE) {
  _tcscpy_s(service_name, PMEM_SERVICE_NAME);
  _tcscpy_s(last_error, TEXT(""));
  max_physical_memory_ = 0;
  }

WinPmem::~WinPmem() {
  if (fd_ != INVALID_HANDLE_VALUE) {
    CloseHandle(fd_);
  }
}

void WinPmem::LogError(const TCHAR *message) {
  _tcsncpy_s(last_error, message, sizeof(last_error));
  if (suppress_output) return;

  _tprintf(TEXT("%s"), message);
};

void WinPmem::Log(const TCHAR *message, ...) {
  if (suppress_output) return;

  va_list ap;
  va_start(ap, message);
  _vtprintf(message, ap);
  va_end(ap);
};

// Roman Dementiev (Intel): added delete_driver option (default is true)
int WinPmem::install_driver(bool delete_driver) {
  SC_HANDLE scm, service;
  int status = -1;

  // Try to load the driver from the resource section.
  if (load_driver_() < 0)
    goto error;

  uninstall_driver();

  scm = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
  if (!scm) {
    LogError(TEXT("Can not open SCM. Are you administrator?"));
    goto error;
  }

  service = CreateService(scm,
                          service_name,
                          service_name,
                          SERVICE_ALL_ACCESS,
                          SERVICE_KERNEL_DRIVER,
                          SERVICE_DEMAND_START,
                          SERVICE_ERROR_NORMAL,
                          driver_filename,
                          NULL,
                          NULL,
                          NULL,
                          NULL,
                          NULL);

  if (GetLastError() == ERROR_SERVICE_EXISTS) {
    CloseServiceHandle(service);
    service = OpenService(scm, service_name, SERVICE_ALL_ACCESS);
  }

  if (!service) {
    CloseServiceHandle(scm);
    goto error;
  };
  if (!StartService(service, 0, NULL)) {
    if (GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
      LogError(TEXT("Error: StartService(), Cannot start the driver.\n"));
      goto service_error;
    }
  }

  Log(TEXT("Loaded Driver %s.\n"), driver_filename);

  fd_ = CreateFile(TEXT("\\\\.\\") TEXT(PMEM_DEVICE_NAME),
                   // Write is needed for IOCTL.
                   GENERIC_READ | GENERIC_WRITE,
                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                   NULL,
                   OPEN_EXISTING,
                   FILE_ATTRIBUTE_NORMAL,
                   NULL);

  if(fd_ == INVALID_HANDLE_VALUE) {
    LogError(TEXT("Can not open raw device."));
    status = -1;
  }
  else
    status = 1;

 service_error:
  CloseServiceHandle(service);
  CloseServiceHandle(scm);

  if(status == 1) restrictDriverAccess(TEXT("\\\\.\\") TEXT(PMEM_DEVICE_NAME));
  if(delete_driver) DeleteFile(driver_filename);

 error:
  return status;
}

int WinPmem::uninstall_driver() {
  SC_HANDLE scm, service;
  SERVICE_STATUS ServiceStatus;

  scm = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);

  if (!scm) return 0;

  service = OpenService(scm, service_name, SERVICE_ALL_ACCESS);

  if (service) {
    ControlService(service, SERVICE_CONTROL_STOP, &ServiceStatus);
  };

  DeleteService(service);
  CloseServiceHandle(service);
  Log(TEXT("Driver Unloaded.\n"));

  CloseServiceHandle(scm);

  return 1;
}

} // namespace pcm

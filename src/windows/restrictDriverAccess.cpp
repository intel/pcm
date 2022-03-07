// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2016, Intel Corporation
#include <windows.h>
#include <iostream>

namespace pcm {

//! restrict usage of driver to system (SY) and builtin admins (BA)
void restrictDriverAccess(LPCWSTR path)
{
    try {
        System::Security::AccessControl::FileSecurity^ fSecurity = System::IO::File::GetAccessControl(gcnew System::String(path));
        fSecurity->SetSecurityDescriptorSddlForm("O:BAG:SYD:(A;;FA;;;SY)(A;;FA;;;BA)");
        System::IO::File::SetAccessControl(gcnew System::String(path), fSecurity);
    }
    catch (...)
    {
        std::wcerr << "Error in GetAccessControl/SetSecurityDescriptorSddlForm for " << path << " driver.\n";
    }
}

} // namespace pcm
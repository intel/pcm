// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2016, Intel Corporation
#include <windows.h>
#include <iostream>

namespace pcm {

#ifdef _MSC_VER
#ifdef UNICODE
    static auto& tcerr = std::wcerr;
#else
    static auto& tcerr = std::cerr;
#endif
#endif // _MSC_VER

//! restrict usage of driver to system (SY) and builtin admins (BA)
void restrictDriverAccess(LPCTSTR path)
{
    try {
        System::Security::AccessControl::FileSecurity^ fSecurity = System::IO::File::GetAccessControl(gcnew System::String(path));
        fSecurity->SetSecurityDescriptorSddlForm("O:BAG:SYD:(A;;FA;;;SY)(A;;FA;;;BA)");
        System::IO::File::SetAccessControl(gcnew System::String(path), fSecurity);
    }
    catch (...)
    {
        tcerr << "Error in GetAccessControl/SetSecurityDescriptorSddlForm for " << path << " driver.\n";
    }
}

} // namespace pcm
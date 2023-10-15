// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation
/*
** Written by Otto Bruggeman and Roman Dementiev
*/

// PCMService.cpp : main Windows Service project file.

#include "PCMService.h"

#include <stdio.h>
#include <tchar.h>
#include <string.h>

using namespace PCMServiceNS;
using namespace System::Text;
using namespace System::Security::Policy;
using namespace System::Reflection;

//To install/uninstall the service, type: "PCM-Service.exe [-Install/-Uninstall]"
int _tmain(int argc, _TCHAR* argv[])
{
	PCM_SET_DLL_DIR
	if (argc >= 2)
	{
		if (argv[1][0] == _T('/'))
		{
			argv[1][0] = _T('-');
		}

		if (_tcsicmp(argv[1], _T("-Install")) == 0)
		{
			array<String^>^ myargs = System::Environment::GetCommandLineArgs();
			array<String^>^ args = gcnew array<String^>(myargs->Length - 1);

			// Set args[0] with the full path to the assembly,
			Assembly^ assem = Assembly::GetExecutingAssembly();
			args[0] = assem->Location;

			Array::Copy(myargs, 2, args, 1, args->Length - 1);
			AppDomain^ dom = AppDomain::CreateDomain(L"execDom");
			Type^ type = System::Object::typeid;
			String^ path = type->Assembly->Location;
			StringBuilder^ sb = gcnew StringBuilder(path->Substring(0, path->LastIndexOf(L"\\")));
			sb->Append(L"\\InstallUtil.exe");
			dom->ExecuteAssembly(sb->ToString(), args);
		}
		else
		if (_tcsicmp(argv[1], _T("-Uninstall")) == 0)
		{
			array<String^>^ myargs = System::Environment::GetCommandLineArgs();
			array<String^>^ args = gcnew array<String^>(2);

			args[0] = L"-u";

			// Set args[0] with the full path to the assembly,
			Assembly^ assem = Assembly::GetExecutingAssembly();
			args[1] = assem->Location;

			AppDomain^ dom = AppDomain::CreateDomain(L"execDom");
			Type^ type = System::Object::typeid;
			String^ path = type->Assembly->Location;
			StringBuilder^ sb = gcnew StringBuilder(path->Substring(0, path->LastIndexOf(L"\\")));
			sb->Append(L"\\InstallUtil.exe");
			dom->ExecuteAssembly(sb->ToString(), args);
		}

	}
	else
	{
        ServiceBase::Run(gcnew PCMService);
	}
}

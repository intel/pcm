//-----------------------------------------------------------------------------
//     Author : hiyohiyo
//       Mail : hiyohiyo@crystalmark.info
//        Web : http://openlibsys.org/
//    License : The modified BSD license
//
//                     Copyright 2007-2009 OpenLibSys.org. All rights reserved.
//-----------------------------------------------------------------------------
// for WinRing0 1.3.x

#pragma once

#include "OlsDef.h"
#include "OlsApiInitDef.h"

//-----------------------------------------------------------------------------
//
// Prototypes
//
//-----------------------------------------------------------------------------

BOOL InitOpenLibSys(HMODULE *hModule);
BOOL DeinitOpenLibSys(HMODULE *hModule);

//-----------------------------------------------------------------------------
//
// Funtions
//
//-----------------------------------------------------------------------------

// DLL
_GetDllStatus GetDllStatus = NULL;
_GetDllVersion GetDllVersion = NULL;
_GetDriverVersion GetDriverVersion = NULL;
_GetDriverType GetDriverType = NULL;

_InitializeOls InitializeOls = NULL;
_DeinitializeOls DeinitializeOls = NULL;

// CPU
_IsCpuid IsCpuid = NULL;
_IsMsr IsMsr = NULL;
_IsTsc IsTsc = NULL;

_Hlt Hlt = NULL;
_Rdmsr Rdmsr = NULL;
_Wrmsr Wrmsr = NULL;
_Rdpmc Rdpmc = NULL;
_Cpuid Cpuid = NULL;
_Rdtsc Rdtsc = NULL;

_HltTx HltTx = NULL;
_RdmsrTx RdmsrTx = NULL;
_WrmsrTx WrmsrTx = NULL;
_RdpmcTx RdpmcTx = NULL;
_CpuidTx CpuidTx = NULL;
_RdtscTx RdtscTx = NULL;

_HltPx HltPx = NULL;
_RdmsrPx RdmsrPx = NULL;
_WrmsrPx WrmsrPx = NULL;
_RdpmcPx RdpmcPx = NULL;
_CpuidPx CpuidPx = NULL;
_RdtscPx RdtscPx = NULL;

// I/O
_ReadIoPortByte ReadIoPortByte = NULL;
_ReadIoPortWord ReadIoPortWord = NULL;
_ReadIoPortDword ReadIoPortDword = NULL;

_ReadIoPortByteEx ReadIoPortByteEx = NULL;
_ReadIoPortWordEx ReadIoPortWordEx = NULL;
_ReadIoPortDwordEx ReadIoPortDwordEx = NULL;

_WriteIoPortByte WriteIoPortByte = NULL;
_WriteIoPortWord WriteIoPortWord = NULL;
_WriteIoPortDword WriteIoPortDword = NULL;

_WriteIoPortByteEx WriteIoPortByteEx = NULL;
_WriteIoPortWordEx WriteIoPortWordEx = NULL;
_WriteIoPortDwordEx WriteIoPortDwordEx = NULL;

// PCI
_SetPciMaxBusIndex SetPciMaxBusIndex = NULL;

_ReadPciConfigByte ReadPciConfigByte = NULL;
_ReadPciConfigWord ReadPciConfigWord = NULL;
_ReadPciConfigDword ReadPciConfigDword = NULL;

_ReadPciConfigByteEx ReadPciConfigByteEx = NULL;
_ReadPciConfigWordEx ReadPciConfigWordEx = NULL;
_ReadPciConfigDwordEx ReadPciConfigDwordEx = NULL;

_WritePciConfigByte WritePciConfigByte = NULL;
_WritePciConfigWord WritePciConfigWord = NULL;
_WritePciConfigDword WritePciConfigDword = NULL;

_WritePciConfigByteEx WritePciConfigByteEx = NULL;
_WritePciConfigWordEx WritePciConfigWordEx = NULL;
_WritePciConfigDwordEx WritePciConfigDwordEx = NULL;

_FindPciDeviceById FindPciDeviceById = NULL;
_FindPciDeviceByClass FindPciDeviceByClass = NULL;

// Memory
#ifdef _PHYSICAL_MEMORY_SUPPORT
_ReadDmiMemory ReadDmiMemory = NULL;
_ReadPhysicalMemory ReadPhysicalMemory = NULL;
_WritePhysicalMemory WritePhysicalMemory = NULL;
#endif

#ifdef _OPEN_LIB_SYS
#ifdef _UNICODE
#define GetOlsString GetOlsStringW
#else
#define GetOlsString GetOlsStringA
#endif

_InstallOpenLibSys InstallOpenLibSys = NULL;
_UninstallOpenLibSys UninstallOpenLibSys = NULL;
_GetDriverStatus GetDriverStatus = NULL;

_GetOlsStringA GetOlsStringA = NULL;
_GetOlsStringW GetOlsStringW = NULL;
_GetOlsValue GetOlsValue = NULL;
_SetOlsValue SetOlsValue = NULL;
#endif

//-----------------------------------------------------------------------------
//
// Initialize
//
//-----------------------------------------------------------------------------

BOOL InitOpenLibSys(HMODULE *hModule)
{
#ifdef _M_X64
	*hModule = LoadLibrary(_T("WinRing0x64.dll"));
#else
	*hModule = LoadLibrary(_T("WinRing0.dll"));
#endif

	if(*hModule == NULL)
	{
		return FALSE;
	}

	//-----------------------------------------------------------------------------
	// GetProcAddress
	//-----------------------------------------------------------------------------
	// DLL
	GetDllStatus =			(_GetDllStatus)			GetProcAddress (*hModule, "GetDllStatus");
	GetDllVersion =			(_GetDllVersion)		GetProcAddress (*hModule, "GetDllVersion");
	GetDriverVersion =		(_GetDriverVersion)		GetProcAddress (*hModule, "GetDriverVersion");
	GetDriverType =			(_GetDriverType)		GetProcAddress (*hModule, "GetDriverType");

	InitializeOls =			(_InitializeOls)		GetProcAddress (*hModule, "InitializeOls");
	DeinitializeOls =		(_DeinitializeOls)		GetProcAddress (*hModule, "DeinitializeOls");

	// CPU
	IsCpuid =				(_IsCpuid)				GetProcAddress (*hModule, "IsCpuid");
	IsMsr =					(_IsMsr)				GetProcAddress (*hModule, "IsMsr");
	IsTsc =					(_IsTsc)				GetProcAddress (*hModule, "IsTsc");
	Hlt =					(_Hlt)					GetProcAddress (*hModule, "Hlt");
	Rdmsr =					(_Rdmsr)				GetProcAddress (*hModule, "Rdmsr");
	Wrmsr =					(_Wrmsr)				GetProcAddress (*hModule, "Wrmsr");
	Rdpmc =					(_Rdpmc)				GetProcAddress (*hModule, "Rdpmc");
	Cpuid =					(_Cpuid)				GetProcAddress (*hModule, "Cpuid");
	Rdtsc =					(_Rdtsc)				GetProcAddress (*hModule, "Rdtsc");
	HltTx =					(_HltTx)				GetProcAddress (*hModule, "HltTx");
	RdmsrTx =				(_RdmsrTx)				GetProcAddress (*hModule, "RdmsrTx");
	WrmsrTx =				(_WrmsrTx)				GetProcAddress (*hModule, "WrmsrTx");
	RdpmcTx =				(_RdpmcTx)				GetProcAddress (*hModule, "RdpmcTx");
	CpuidTx =				(_CpuidTx)				GetProcAddress (*hModule, "CpuidTx");
	RdtscTx =				(_RdtscTx)				GetProcAddress (*hModule, "RdtscTx");
	HltPx =					(_HltPx)				GetProcAddress (*hModule, "HltPx");
	RdmsrPx =				(_RdmsrPx)				GetProcAddress (*hModule, "RdmsrPx");
	WrmsrPx =				(_WrmsrPx)				GetProcAddress (*hModule, "WrmsrPx");
	RdpmcPx =				(_RdpmcPx)				GetProcAddress (*hModule, "RdpmcPx");
	CpuidPx =				(_CpuidPx)				GetProcAddress (*hModule, "CpuidPx");
	RdtscPx =				(_RdtscPx)				GetProcAddress (*hModule, "RdtscPx");

	// I/O
	ReadIoPortByte =		(_ReadIoPortByte)		GetProcAddress (*hModule, "ReadIoPortByte");
	ReadIoPortWord =		(_ReadIoPortWord)		GetProcAddress (*hModule, "ReadIoPortWord");
	ReadIoPortDword =		(_ReadIoPortDword)		GetProcAddress (*hModule, "ReadIoPortDword");

	ReadIoPortByteEx =		(_ReadIoPortByteEx)		GetProcAddress (*hModule, "ReadIoPortByteEx");
	ReadIoPortWordEx =		(_ReadIoPortWordEx)		GetProcAddress (*hModule, "ReadIoPortWordEx");
	ReadIoPortDwordEx =		(_ReadIoPortDwordEx)	GetProcAddress (*hModule, "ReadIoPortDwordEx");

	WriteIoPortByte =		(_WriteIoPortByte)		GetProcAddress (*hModule, "WriteIoPortByte");
	WriteIoPortWord =		(_WriteIoPortWord)		GetProcAddress (*hModule, "WriteIoPortWord");
	WriteIoPortDword =		(_WriteIoPortDword)		GetProcAddress (*hModule, "WriteIoPortDword");

	WriteIoPortByteEx =		(_WriteIoPortByteEx)	GetProcAddress (*hModule, "WriteIoPortByteEx");
	WriteIoPortWordEx =		(_WriteIoPortWordEx)	GetProcAddress (*hModule, "WriteIoPortWordEx");
	WriteIoPortDwordEx =	(_WriteIoPortDwordEx)	GetProcAddress (*hModule, "WriteIoPortDwordEx");

	// PCI
	SetPciMaxBusIndex =		(_SetPciMaxBusIndex)	GetProcAddress (*hModule, "SetPciMaxBusIndex");

	ReadPciConfigByte =		(_ReadPciConfigByte)	GetProcAddress (*hModule, "ReadPciConfigByte");
	ReadPciConfigWord =		(_ReadPciConfigWord)	GetProcAddress (*hModule, "ReadPciConfigWord");
	ReadPciConfigDword =	(_ReadPciConfigDword)	GetProcAddress (*hModule, "ReadPciConfigDword");

	ReadPciConfigByteEx =	(_ReadPciConfigByteEx)	GetProcAddress (*hModule, "ReadPciConfigByteEx");
	ReadPciConfigWordEx =	(_ReadPciConfigWordEx)	GetProcAddress (*hModule, "ReadPciConfigWordEx");
	ReadPciConfigDwordEx =	(_ReadPciConfigDwordEx)	GetProcAddress (*hModule, "ReadPciConfigDwordEx");

	WritePciConfigByte =	(_WritePciConfigByte)	GetProcAddress (*hModule, "WritePciConfigByte");
	WritePciConfigWord =	(_WritePciConfigWord)	GetProcAddress (*hModule, "WritePciConfigWord");
	WritePciConfigDword =	(_WritePciConfigDword)	GetProcAddress (*hModule, "WritePciConfigDword");

	WritePciConfigByteEx =	(_WritePciConfigByteEx)	GetProcAddress (*hModule, "WritePciConfigByteEx");
	WritePciConfigWordEx =	(_WritePciConfigWordEx)	GetProcAddress (*hModule, "WritePciConfigWordEx");
	WritePciConfigDwordEx =	(_WritePciConfigDwordEx)GetProcAddress (*hModule, "WritePciConfigDwordEx");

	FindPciDeviceById =		(_FindPciDeviceById)	GetProcAddress (*hModule, "FindPciDeviceById");
	FindPciDeviceByClass =	(_FindPciDeviceByClass)	GetProcAddress (*hModule, "FindPciDeviceByClass");

	// Memory
#ifdef _PHYSICAL_MEMORY_SUPPORT
	ReadDmiMemory =			(_ReadDmiMemory)		GetProcAddress (*hModule, "ReadDmiMemory");
	ReadPhysicalMemory =	(_ReadPhysicalMemory)	GetProcAddress (*hModule, "ReadPhysicalMemory");
	WritePhysicalMemory =	(_WritePhysicalMemory)	GetProcAddress (*hModule, "WritePhysicalMemory");
#endif

	//-----------------------------------------------------------------------------
	// Check Functions
	//-----------------------------------------------------------------------------
	if(!(
		GetDllStatus
	&&	GetDllVersion
	&&	GetDriverVersion
	&&	GetDriverType
	&&	InitializeOls
	&&	DeinitializeOls
	&&	IsCpuid
	&&	IsMsr
	&&	IsTsc
	&&	Hlt
	&&	HltTx
	&&	HltPx
	&&	Rdmsr
	&&	RdmsrTx
	&&	RdmsrPx
	&&	Wrmsr
	&&	WrmsrTx
	&&	WrmsrPx
	&&	Rdpmc
	&&	RdpmcTx
	&&	RdpmcPx
	&&	Cpuid
	&&	CpuidTx
	&&	CpuidPx
	&&	Rdtsc
	&&	RdtscTx
	&&	RdtscPx
	&&	ReadIoPortByte
	&&	ReadIoPortWord
	&&	ReadIoPortDword
	&&	ReadIoPortByteEx
	&&	ReadIoPortWordEx
	&&	ReadIoPortDwordEx
	&&	WriteIoPortByte
	&&	WriteIoPortWord
	&&	WriteIoPortDword
	&&	WriteIoPortByteEx
	&&	WriteIoPortWordEx
	&&	WriteIoPortDwordEx
	&&	SetPciMaxBusIndex
	&&	ReadPciConfigByte
	&&	ReadPciConfigWord
	&&	ReadPciConfigDword
	&&	ReadPciConfigByteEx
	&&	ReadPciConfigWordEx
	&&	ReadPciConfigDwordEx
	&&	WritePciConfigByte
	&&	WritePciConfigWord 
	&&	WritePciConfigDword
	&&	WritePciConfigByteEx
	&&	WritePciConfigWordEx 
	&&	WritePciConfigDwordEx
	&&	FindPciDeviceById
	&&	FindPciDeviceByClass
#ifdef _PHYSICAL_MEMORY_SUPPORT
	&&	ReadDmiMemory
	&&	ReadPhysicalMemory
	&&	WritePhysicalMemory
#endif
	))
	{
		return FALSE;
	}

	return InitializeOls();
}

//-----------------------------------------------------------------------------
//
// Deinitialize
//
//-----------------------------------------------------------------------------

BOOL DeinitOpenLibSys(HMODULE *hModule)
{
	BOOL result = FALSE;

	if(*hModule == NULL)
	{
		return TRUE;
	}
	else
	{
		DeinitializeOls();
		result = FreeLibrary(*hModule);
		*hModule = NULL;

		return result;
	}
}

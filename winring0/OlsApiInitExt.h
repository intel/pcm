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

#include "OlsApiInitDef.h"

//-----------------------------------------------------------------------------
//
// Externs
//
//-----------------------------------------------------------------------------

// DLL
extern _GetDllStatus GetDllStatus;
extern _GetDllVersion GetDllVersion;
extern _GetDriverVersion GetDriverVersion;
extern _GetDriverType GetDriverType;

extern _InitializeOls InitializeOls;
extern _DeinitializeOls DeinitializeOls;

// CPU
extern _IsCpuid IsCpuid;
extern _IsMsr IsMsr;
extern _IsTsc IsTsc;

extern _Hlt Hlt;
extern _Rdmsr Rdmsr;
extern _Wrmsr Wrmsr;
extern _Rdpmc Rdpmc;
extern _Cpuid Cpuid;
extern _Rdtsc Rdtsc;

extern _HltTx HltTx;
extern _RdmsrTx RdmsrTx;
extern _WrmsrTx WrmsrTx;
extern _RdpmcTx RdpmcTx;
extern _CpuidTx CpuidTx;
extern _RdtscTx RdtscTx;

extern _HltPx HltPx;
extern _RdmsrPx RdmsrPx;
extern _WrmsrPx WrmsrPx;
extern _RdpmcPx RdpmcPx;
extern _CpuidPx CpuidPx;
extern _RdtscPx RdtscPx;

// I/O
extern _ReadIoPortByte ReadIoPortByte;
extern _ReadIoPortWord ReadIoPortWord;
extern _ReadIoPortDword ReadIoPortDword;

extern _ReadIoPortByteEx ReadIoPortByteEx;
extern _ReadIoPortWordEx ReadIoPortWordEx;
extern _ReadIoPortDwordEx ReadIoPortDwordEx;

extern _WriteIoPortByte WriteIoPortByte;
extern _WriteIoPortWord WriteIoPortWord;
extern _WriteIoPortDword WriteIoPortDword;

extern _WriteIoPortByteEx WriteIoPortByteEx;
extern _WriteIoPortWordEx WriteIoPortWordEx;
extern _WriteIoPortDwordEx WriteIoPortDwordEx;

// PCI
extern _SetPciMaxBusIndex SetPciMaxBusIndex;

extern _ReadPciConfigByte ReadPciConfigByte;
extern _ReadPciConfigWord ReadPciConfigWord;
extern _ReadPciConfigDword ReadPciConfigDword;

extern _ReadPciConfigByteEx ReadPciConfigByteEx;
extern _ReadPciConfigWordEx ReadPciConfigWordEx;
extern _ReadPciConfigDwordEx ReadPciConfigDwordEx;

extern _WritePciConfigByte WritePciConfigByte;
extern _WritePciConfigWord WritePciConfigWord;
extern _WritePciConfigDword WritePciConfigDword;

extern _WritePciConfigByteEx WritePciConfigByteEx;
extern _WritePciConfigWordEx WritePciConfigWordEx;
extern _WritePciConfigDwordEx WritePciConfigDwordEx;

extern _FindPciDeviceById FindPciDeviceById;
extern _FindPciDeviceByClass FindPciDeviceByClass;

// Memory
#ifdef _PHYSICAL_MEMORY_SUPPORT
extern _ReadDmiMemory ReadDmiMemory;
extern _ReadPhysicalMemory ReadPhysicalMemory;
extern _WritePhysicalMemory WritePhysicalMemory;
#endif

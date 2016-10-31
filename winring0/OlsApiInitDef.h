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

//-----------------------------------------------------------------------------
//
// Type Defines
//
//-----------------------------------------------------------------------------

// DLL
typedef DWORD (WINAPI *_GetDllStatus) ();
typedef DWORD (WINAPI *_GetDllVersion) (PBYTE major, PBYTE minor, PBYTE revision, PBYTE release);
typedef DWORD (WINAPI *_GetDriverVersion) (PBYTE major, PBYTE minor, PBYTE revision, PBYTE release);
typedef DWORD (WINAPI *_GetDriverType) ();

typedef BOOL (WINAPI *_InitializeOls) ();
typedef VOID (WINAPI *_DeinitializeOls) ();

// CPU
typedef BOOL (WINAPI *_IsCpuid) ();
typedef BOOL (WINAPI *_IsMsr) ();
typedef BOOL (WINAPI *_IsTsc) ();

typedef BOOL  (WINAPI *_Hlt) ();
typedef DWORD (WINAPI *_Rdmsr) (DWORD index, PDWORD eax, PDWORD edx);
typedef DWORD (WINAPI *_Wrmsr) (DWORD index, DWORD eax, DWORD edx);
typedef DWORD (WINAPI *_Rdpmc) (DWORD index, PDWORD eax, PDWORD edx);
typedef DWORD (WINAPI *_Cpuid) (DWORD index, PDWORD eax, PDWORD ebx, PDWORD ecx, PDWORD edx);
typedef DWORD (WINAPI *_Rdtsc) (PDWORD eax, PDWORD edx);

typedef BOOL  (WINAPI *_HltTx) (DWORD_PTR threadAffinityMask);
typedef DWORD (WINAPI *_RdmsrTx) (DWORD index, PDWORD eax, PDWORD edx, DWORD_PTR threadAffinityMask);
typedef DWORD (WINAPI *_WrmsrTx) (DWORD index, DWORD eax, DWORD edx, DWORD_PTR threadAffinityMask);
typedef DWORD (WINAPI *_RdpmcTx) (DWORD index, PDWORD eax, PDWORD edx, DWORD_PTR threadAffinityMask);
typedef DWORD (WINAPI *_CpuidTx) (DWORD index, PDWORD eax, PDWORD ebx, PDWORD ecx, PDWORD edx, DWORD_PTR threadAffinityMask);
typedef DWORD (WINAPI *_RdtscTx) (PDWORD eax, PDWORD edx, DWORD_PTR threadAffinityMask);

typedef BOOL  (WINAPI *_HltPx)   (DWORD_PTR processAffinityMask);
typedef DWORD (WINAPI *_RdmsrPx) (DWORD index, PDWORD eax, PDWORD edx, DWORD_PTR processAffinityMask);
typedef DWORD (WINAPI *_WrmsrPx) (DWORD index, DWORD eax, DWORD edx, DWORD_PTR processAffinityMask);
typedef DWORD (WINAPI *_RdpmcPx) (DWORD index, PDWORD eax, PDWORD edx, DWORD_PTR processAffinityMask);
typedef DWORD (WINAPI *_CpuidPx) (DWORD index, PDWORD eax, PDWORD ebx, PDWORD ecx, PDWORD edx, DWORD_PTR processAffinityMask);
typedef DWORD (WINAPI *_RdtscPx) (PDWORD eax, PDWORD edx, DWORD_PTR processAffinityMask);

// I/O
typedef BYTE  (WINAPI *_ReadIoPortByte) (WORD address);
typedef WORD  (WINAPI *_ReadIoPortWord) (WORD address);
typedef DWORD (WINAPI *_ReadIoPortDword) (WORD address);

typedef BOOL (WINAPI *_ReadIoPortByteEx) (WORD address, PBYTE value);
typedef BOOL (WINAPI *_ReadIoPortWordEx) (WORD address, PWORD value);
typedef BOOL (WINAPI *_ReadIoPortDwordEx) (WORD address, PDWORD value);

typedef VOID (WINAPI *_WriteIoPortByte) (WORD address, BYTE value);
typedef VOID (WINAPI *_WriteIoPortWord) (WORD address, WORD value);
typedef VOID (WINAPI *_WriteIoPortDword) (WORD address, DWORD value);

typedef BOOL (WINAPI *_WriteIoPortByteEx) (WORD address, BYTE value);
typedef BOOL (WINAPI *_WriteIoPortWordEx) (WORD address, WORD value);
typedef BOOL (WINAPI *_WriteIoPortDwordEx) (WORD address, DWORD value);

// PCI
typedef VOID (WINAPI *_SetPciMaxBusIndex) (BYTE max);

typedef BYTE  (WINAPI *_ReadPciConfigByte) (DWORD pciAddress, BYTE regAddress);
typedef WORD  (WINAPI *_ReadPciConfigWord) (DWORD pciAddress, BYTE regAddress);
typedef DWORD (WINAPI *_ReadPciConfigDword) (DWORD pciAddress, BYTE regAddress);

typedef BOOL (WINAPI *_ReadPciConfigByteEx) (DWORD pciAddress, DWORD regAddress, PBYTE value);
typedef BOOL (WINAPI *_ReadPciConfigWordEx) (DWORD pciAddress, DWORD regAddress, PWORD value);
typedef BOOL (WINAPI *_ReadPciConfigDwordEx) (DWORD pciAddress, DWORD regAddress, PDWORD value);

typedef VOID (WINAPI *_WritePciConfigByte) (DWORD pciAddress, BYTE regAddress, BYTE value);
typedef VOID (WINAPI *_WritePciConfigWord) (DWORD pciAddress, BYTE regAddress, WORD value);
typedef VOID (WINAPI *_WritePciConfigDword) (DWORD pciAddress, BYTE regAddress, DWORD value);

typedef BOOL (WINAPI *_WritePciConfigByteEx) (DWORD pciAddress, DWORD regAddress, BYTE value);
typedef BOOL (WINAPI *_WritePciConfigWordEx) (DWORD pciAddress, DWORD regAddress, WORD value);
typedef BOOL (WINAPI *_WritePciConfigDwordEx) (DWORD pciAddress, DWORD regAddress, DWORD value);

typedef DWORD (WINAPI *_FindPciDeviceById) (WORD vendorId, WORD deviceId, BYTE index);
typedef DWORD (WINAPI *_FindPciDeviceByClass) (BYTE baseClass, BYTE subClass, BYTE programIf, BYTE index);

// Memory
#ifdef _PHYSICAL_MEMORY_SUPPORT
typedef DWORD (WINAPI *_ReadDmiMemory) (PBYTE buffer, DWORD count, DWORD unitSize);
typedef DWORD (WINAPI *_ReadPhysicalMemory) (DWORD_PTR address, PBYTE buffer, DWORD count, DWORD unitSize);
typedef DWORD (WINAPI *_WritePhysicalMemory) (DWORD_PTR address, PBYTE buffer, DWORD count, DWORD unitSize);
#endif

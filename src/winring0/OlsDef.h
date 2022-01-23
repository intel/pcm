//-----------------------------------------------------------------------------
//     Author : hiyohiyo
//       Mail : hiyohiyo@crystalmark.info
//        Web : http://openlibsys.org/
//    License : The modified BSD license
//
//                          Copyright 2007 OpenLibSys.org. All rights reserved.
//-----------------------------------------------------------------------------

#pragma once

//-----------------------------------------------------------------------------
//
// DLL Status Code
//
//-----------------------------------------------------------------------------

#define OLS_DLL_NO_ERROR						0
#define OLS_DLL_UNSUPPORTED_PLATFORM			1
#define OLS_DLL_DRIVER_NOT_LOADED				2
#define OLS_DLL_DRIVER_NOT_FOUND				3
#define OLS_DLL_DRIVER_UNLOADED					4
#define OLS_DLL_DRIVER_NOT_LOADED_ON_NETWORK	5
#define OLS_DLL_UNKNOWN_ERROR					9

//-----------------------------------------------------------------------------
//
// Driver Type
//
//-----------------------------------------------------------------------------

#define OLS_DRIVER_TYPE_UNKNOWN			0
#define OLS_DRIVER_TYPE_WIN_9X			1
#define OLS_DRIVER_TYPE_WIN_NT			2
#define OLS_DRIVER_TYPE_WIN_NT4			3	// Obsolete
#define OLS_DRIVER_TYPE_WIN_NT_X64		4
#define OLS_DRIVER_TYPE_WIN_NT_IA64		5	// Reseved

//-----------------------------------------------------------------------------
//
// PCI Error Code
//
//-----------------------------------------------------------------------------

#define OLS_ERROR_PCI_BUS_NOT_EXIST		(0xE0000001L)
#define OLS_ERROR_PCI_NO_DEVICE			(0xE0000002L)
#define OLS_ERROR_PCI_WRITE_CONFIG		(0xE0000003L)
#define OLS_ERROR_PCI_READ_CONFIG		(0xE0000004L)

//-----------------------------------------------------------------------------
//
// Support Macros
//
//-----------------------------------------------------------------------------

// Bus Number, Device Number and Function Number to PCI Device Address
#define PciBusDevFunc(Bus, Dev, Func)	((Bus&0xFF)<<8) | ((Dev&0x1F)<<3) | (Func&7)
// PCI Device Address to Bus Number
#define PciGetBus(address)				((address>>8) & 0xFF)
// PCI Device Address to Device Number
#define PciGetDev(address)				((address>>3) & 0x1F)
// PCI Device Address to Function Number
#define PciGetFunc(address)				(address&7)

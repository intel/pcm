#pragma once

#include "ntdef.h"

#ifndef __cplusplus
typedef unsigned char bool;
static const bool true = 1;
static const bool false = 0;
#endif

bool AllowMSRAccess(ULONG64 msrAddress);
bool AllowPCICFGAccess(UINT32 device, UINT32 offset);

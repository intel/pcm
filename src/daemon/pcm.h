// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation
// written by Steven Briscoe

#ifndef PCM_H_
#define PCM_H_

#ifdef _MSC_VER
	#include <windows.h>
	#include "../../../PCM_Win/windriver.h"
#else
	#include <unistd.h>
	#include <signal.h>
	#include <sys/time.h> // for gettimeofday()
#endif
#include "../cpucounters.h"
#include "../utils.h"
#ifdef _MSC_VER
	#include "../../freegetopt/getopt.h"
#endif

using namespace pcm;

#endif /* PCM_H_ */

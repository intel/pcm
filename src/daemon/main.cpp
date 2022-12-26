// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation
// written by Steven Briscoe

#include "daemon.h"

PCM_MAIN_NOTHROW;

int mainThrows(int argc, char * argv[])
{
	PCMDaemon::Daemon daemon(argc, argv);

	return daemon.run();
}

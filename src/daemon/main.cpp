// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2017, Intel Corporation
// written by Steven Briscoe

#include "daemon.h"

int main(int argc, char *argv[])
{
	PCMDaemon::Daemon daemon(argc, argv);

	return daemon.run();
}

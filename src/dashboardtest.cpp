// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2020-2022, Intel Corporation

#include "dashboard.h"
#include <iostream>

int main()
{
    std::cout << pcm::getPCMDashboardJSON(pcm::Prometheus, 2, 3, 10) << std::endl;
    return 0;
}

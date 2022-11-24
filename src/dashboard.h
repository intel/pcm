// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2020-2022, Intel Corporation
#pragma once
#include <string>

namespace pcm {

enum PCMDashboardType { InfluxDB, Prometheus, Prometheus_Default };

std::string getPCMDashboardJSON(const PCMDashboardType type, int ns = -1, int nu = -1, int nc = -1);

} // namespace pcm

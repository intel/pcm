// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2016-2022, Intel Corporation
#include <windows.h>
#include <iostream>

namespace pcm {

#ifdef _MSC_VER
#ifdef UNICODE
    static auto& tcerr = std::wcerr;
#else
    static auto& tcerr = std::cerr;
#endif
#endif // _MSC_VER

} // namespace pcm

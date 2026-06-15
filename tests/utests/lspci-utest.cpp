// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2025, Intel Corporation
// written by Alexander Antonov

#include "lspci.h"
#include <gtest/gtest.h>
#include <string>

using namespace pcm;

TEST(BDFTest, ToStringDefaultConstructor)
{
    struct bdf default_bdf;
    EXPECT_EQ(default_bdf.to_string(), "0000:00:00.0");
}

TEST(BDFTest, ToStringCustomConstructor)
{
    struct bdf custom_bdf(0x1234, 0x56, 0x10, 0x7);
    EXPECT_EQ(custom_bdf.to_string(), "1234:56:10.7");
}

TEST(BDFTest, ToStringPartialConstructor)
{
    struct bdf partial_bdf(0x56, 0x10, 0x7);
    EXPECT_EQ(partial_bdf.to_string(), "0000:56:10.7");
}

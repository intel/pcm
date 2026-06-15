// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2025, Intel Corporation

#include "utils.h"
#include <gtest/gtest.h>
#include <stdexcept>

using namespace pcm;

// Test valid decimal numbers
TEST(ReadNumberTest, ValidDecimalNumbers)
{
    EXPECT_EQ(read_number("0"), 0ULL);
    EXPECT_EQ(read_number("123"), 123ULL);
    EXPECT_EQ(read_number("456789"), 456789ULL);
    EXPECT_EQ(read_number("18446744073709551615"), 18446744073709551615ULL); // max uint64
}

// Test valid hexadecimal numbers
TEST(ReadNumberTest, ValidHexadecimalNumbers)
{
    EXPECT_EQ(read_number("0x0"), 0ULL);
    EXPECT_EQ(read_number("0x10"), 16ULL);
    EXPECT_EQ(read_number("0xFF"), 255ULL);
    EXPECT_EQ(read_number("0xABCD"), 43981ULL);
    EXPECT_EQ(read_number("0xFFFFFFFFFFFFFFFF"), 18446744073709551615ULL); // max uint64
    EXPECT_EQ(read_number("0Xabcd"), 43981ULL); // capital X
}

// Test invalid inputs - should throw exceptions
TEST(ReadNumberTest, InvalidInputsThrowException)
{
    EXPECT_THROW(read_number(""), std::invalid_argument);
    EXPECT_THROW(read_number("abc"), std::invalid_argument);
    EXPECT_THROW(read_number("12abc"), std::invalid_argument);
    EXPECT_THROW(read_number("0xGHI"), std::invalid_argument);
    EXPECT_THROW(read_number("not a number"), std::invalid_argument);
    EXPECT_THROW(read_number("123.456"), std::invalid_argument);
    EXPECT_THROW(read_number("-123"), std::invalid_argument);
    EXPECT_THROW(read_number("0x"), std::invalid_argument);
    EXPECT_THROW(read_number("x123"), std::invalid_argument);
    EXPECT_THROW(read_number("  "), std::invalid_argument);
}

// Test edge cases with whitespace
TEST(ReadNumberTest, WhitespaceHandling)
{
    // Leading/trailing whitespace should be acceptable
    EXPECT_EQ(read_number(" 123"), 123ULL);
    EXPECT_EQ(read_number("123 "), 123ULL);
    EXPECT_EQ(read_number(" 123 "), 123ULL);
    EXPECT_EQ(read_number(" 0x10 "), 16ULL);
}

// Test numbers with extra characters should throw
TEST(ReadNumberTest, ExtraCharactersThrowException)
{
    EXPECT_THROW(read_number("123abc"), std::invalid_argument);
    EXPECT_THROW(read_number("0x10ZZ"), std::invalid_argument);
    EXPECT_THROW(read_number("12 34"), std::invalid_argument);
}

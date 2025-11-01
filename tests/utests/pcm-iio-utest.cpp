// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2025, Intel Corporation
// written by Alexander Antonov

#include <gtest/gtest.h>
#include <string>
#include <map>
#include <vector>
#include <iostream>
#include "utils.h"
#include "pcm-iio-pmu.h"
#include "pcm-iio-topology.h"

using namespace pcm;

class LoadEventsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        fillOpcodeFieldMapForPCIeEvents(opcodeFieldMap);

        evt_ctx.ctrs.clear();
    }

    std::map<std::string, uint32_t> opcodeFieldMap;
    iio_evt_parse_context evt_ctx;
    PCIeEventNameMap nameMap;
};

// Structure to hold expected event data from file
struct ExpectedEvent {
    int ctr;
    uint32_t ev_sel;
    uint32_t umask;
    uint32_t ch_mask;
    uint32_t fc_mask;
    int multiplier;
    std::string hname;
    std::string vname;
    CounterType type;

    bool operator==(const struct iio_counter& actual) const
    {
        bool basic_match =
            ctr == actual.idx &&
            hname == actual.h_event_name &&
            vname == actual.v_event_name &&
            multiplier == actual.multiplier;

        bool ev_sel_match = (actual.ccr & 0xFF) == ev_sel;
        bool umask_match = ((actual.ccr >> 8) & 0xFF) == umask;

        bool ch_mask_match = (((actual.ccr >> 36) & 0xFFF) == ch_mask);
        bool fc_mask_match = (((actual.ccr >> 48) & 0x7) == fc_mask);

        bool counter_type_match = (actual.type == type);

        return basic_match && ev_sel_match && umask_match && ch_mask_match && fc_mask_match && counter_type_match;
    }
};

TEST_F(LoadEventsTest, TestLoadEventsAlternateVersion)
{
    const std::string eventFile = "opCode-6-174.txt";

    evt_ctx.cpu_family_model = PCM_CPU_FAMILY_MODEL(6, 174);

    ASSERT_NO_THROW({
        load_events(eventFile, opcodeFieldMap, iio_evt_parse_handler, &evt_ctx);
    });

    ASSERT_FALSE(evt_ctx.ctrs.empty()) << "No events were loaded from the file";

    // Verify at least one counter was properly initialized
    bool foundCounterWithProperConfig = false;
    for (const auto& ctr : evt_ctx.ctrs) {
        if (ctr.ccr != 0) {
            foundCounterWithProperConfig = true;
            break;
        }
    }
    EXPECT_TRUE(foundCounterWithProperConfig) << "No properly configured counters found";
}

TEST_F(LoadEventsTest, TestVerifyAllFieldsFromOpcodeFile)
{
    std::vector<ExpectedEvent> expectedEvents = {
        // IB write events
        {0, 0x83, 0x1, 1,  0x7,  4, "IB write", "Part0", CounterType::iio},
        {1, 0x83, 0x1, 2,  0x7,  4, "IB write", "Part1", CounterType::iio},
        {0, 0x83, 0x1, 4,  0x7,  4, "IB write", "Part2", CounterType::iio},
        {1, 0x83, 0x1, 8,  0x7,  4, "IB write", "Part3", CounterType::iio},
        {0, 0x83, 0x1, 16, 0x7,  4, "IB write", "Part4", CounterType::iio},
        {1, 0x83, 0x1, 32, 0x7,  4, "IB write", "Part5", CounterType::iio},
        {0, 0x83, 0x1, 64, 0x7,  4, "IB write", "Part6", CounterType::iio},
        {1, 0x83, 0x1, 128, 0x7, 4, "IB write", "Part7", CounterType::iio},

        // IB read events
        {0, 0x83, 0x4, 1,   0x7, 4, "IB read", "Part0", CounterType::iio},
        {1, 0x83, 0x4, 2,   0x7, 4, "IB read", "Part1", CounterType::iio},
        {0, 0x83, 0x4, 4,   0x7, 4, "IB read", "Part2", CounterType::iio},
        {1, 0x83, 0x4, 8,   0x7, 4, "IB read", "Part3", CounterType::iio},
        {0, 0x83, 0x4, 16,  0x7, 4, "IB read", "Part4", CounterType::iio},
        {1, 0x83, 0x4, 32,  0x7, 4, "IB read", "Part5", CounterType::iio},
        {0, 0x83, 0x4, 64,  0x7, 4, "IB read", "Part6", CounterType::iio},
        {1, 0x83, 0x4, 128, 0x7, 4, "IB read", "Part7", CounterType::iio},

        // OB read events
        {2, 0xc0, 0x4, 1,   0x7, 4, "OB read", "Part0", CounterType::iio},
        {3, 0xc0, 0x4, 2,   0x7, 4, "OB read", "Part1", CounterType::iio},
        {2, 0xc0, 0x4, 4,   0x7, 4, "OB read", "Part2", CounterType::iio},
        {3, 0xc0, 0x4, 8,   0x7, 4, "OB read", "Part3", CounterType::iio},
        {2, 0xc0, 0x4, 16,  0x7, 4, "OB read", "Part4", CounterType::iio},
        {3, 0xc0, 0x4, 32,  0x7, 4, "OB read", "Part5", CounterType::iio},
        {2, 0xc0, 0x4, 64,  0x7, 4, "OB read", "Part6", CounterType::iio},
        {3, 0xc0, 0x4, 128, 0x7, 4, "OB read", "Part7", CounterType::iio},

        // OB write events
        {2, 0xc0, 0x1, 1,   0x7, 4, "OB write", "Part0", CounterType::iio},
        {3, 0xc0, 0x1, 2,   0x7, 4, "OB write", "Part1", CounterType::iio},
        {2, 0xc0, 0x1, 4,   0x7, 4, "OB write", "Part2", CounterType::iio},
        {3, 0xc0, 0x1, 8,   0x7, 4, "OB write", "Part3", CounterType::iio},
        {2, 0xc0, 0x1, 16,  0x7, 4, "OB write", "Part4", CounterType::iio},
        {3, 0xc0, 0x1, 32,  0x7, 4, "OB write", "Part5", CounterType::iio},
        {2, 0xc0, 0x1, 64,  0x7, 4, "OB write", "Part6", CounterType::iio},
        {3, 0xc0, 0x1, 128, 0x7, 4, "OB write", "Part7", CounterType::iio},

        // IOMMU events
        {0, 0x40, 0x01, 0x0, 0x0, 1, "IOTLB Lookup",     "Total", CounterType::iio},
        {1, 0x40, 0x20, 0x0, 0x0, 1, "IOTLB Miss",       "Total", CounterType::iio},
        {2, 0x40, 0x80, 0x0, 0x0, 1, "Ctxt Cache Hit",   "Total", CounterType::iio},
        {3, 0x41, 0x10, 0x0, 0x0, 1, "256T Cache Hit",   "Total", CounterType::iio},
        {0, 0x41, 0x08, 0x0, 0x0, 1, "512G Cache Hit",   "Total", CounterType::iio},
        {1, 0x41, 0x04, 0x0, 0x0, 1, "1G Cache Hit",     "Total", CounterType::iio},
        {2, 0x41, 0x02, 0x0, 0x0, 1, "2M Cache Hit",     "Total", CounterType::iio},
        {3, 0x41, 0xc0, 0x0, 0x0, 1, "IOMMU Mem Access", "Total", CounterType::iio},
    };

    evt_ctx.cpu_family_model = PCM_CPU_FAMILY_MODEL(6, 174);

    evt_ctx.ctrs.clear();
    ASSERT_NO_THROW({
        load_events("opCode-6-174.txt", opcodeFieldMap, iio_evt_parse_handler, &evt_ctx);
    });

    ASSERT_EQ(expectedEvents.size(), evt_ctx.ctrs.size())
        << "Number of loaded events doesn't match expected count";

    std::vector<bool> foundEvents(expectedEvents.size(), false);

    // For each loaded event, find and verify the matching expected event
    for (const auto& actualEvt : evt_ctx.ctrs) {
        bool found = false;
        for (size_t i = 0; i < expectedEvents.size(); ++i) {
            if (!foundEvents[i] && expectedEvents[i] == actualEvt) {
                foundEvents[i] = true;
                found = true;

                EXPECT_EQ(expectedEvents[i].ctr, actualEvt.idx)
                    << "Counter index mismatch for " << actualEvt.h_event_name
                    << "/" << actualEvt.v_event_name;

                EXPECT_EQ(expectedEvents[i].ev_sel, (actualEvt.ccr & 0xFF))
                    << "Event select mismatch for " << actualEvt.h_event_name
                    << "/" << actualEvt.v_event_name;

                EXPECT_EQ(expectedEvents[i].umask, ((actualEvt.ccr >> 8) & 0xFF))
                    << "UMASK mismatch for " << actualEvt.h_event_name
                    << "/" << actualEvt.v_event_name;

                EXPECT_EQ(expectedEvents[i].ch_mask, ((actualEvt.ccr >> 36) & 0xFFF))
                    << "CH_MASK mismatch for " << actualEvt.h_event_name
                    << "/" << actualEvt.v_event_name;

                EXPECT_EQ(expectedEvents[i].fc_mask, ((actualEvt.ccr >> 48) & 0x7))
                    << "FC_MASK mismatch for " << actualEvt.h_event_name
                    << "/" << actualEvt.v_event_name;

                EXPECT_EQ(expectedEvents[i].multiplier, actualEvt.multiplier)
                    << "Multiplier mismatch for " << actualEvt.h_event_name
                    << "/" << actualEvt.v_event_name;

                EXPECT_EQ(expectedEvents[i].type, actualEvt.type)
                    << "Counter type mismatch for " << actualEvt.h_event_name
                    << "/" << actualEvt.v_event_name;

                break;
            }
        }
        EXPECT_TRUE(found) << "Could not find expected event for " << actualEvt.h_event_name << "/" << actualEvt.v_event_name
            << "\nActual event details:"
            << "\n  Counter index: " << actualEvt.idx
            << "\n  Event select: 0x" << std::hex << (actualEvt.ccr & 0xFF) << std::dec
            << "\n  UMASK: 0x" << std::hex << ((actualEvt.ccr >> 8) & 0xFF) << std::dec
            << "\n  CH_MASK: 0x" << std::hex << ((actualEvt.ccr >> 36) & 0xFFF) << std::dec
            << "\n  FC_MASK: 0x" << std::hex << ((actualEvt.ccr >> 48) & 0x7) << std::dec
            << "\n  CCR (full): 0x" << std::hex << actualEvt.ccr << std::dec
            << "\n  Multiplier: " << actualEvt.multiplier
            << "\n  Type: " << static_cast<int>(actualEvt.type);
    }

    // Verify all expected events were found
    for (size_t i = 0; i < foundEvents.size(); ++i) {
        EXPECT_TRUE(foundEvents[i]) << "Expected event " << expectedEvents[i].hname
                                << "/" << expectedEvents[i].vname << " was not loaded";
    }
}

class PcmIioTopologyTestBase: public ::testing::Test
{
};

TEST_F(PcmIioTopologyTestBase, DefaultTopologyTest)
{
    // Use invalid value to trigger default platform mapping
    const uint32_t model = PCM::END_OF_MODEL_LIST;
    const uint32_t sockets = 2;
    const uint32_t stacks = 12;

    const std::vector<std::string> expectedStackNames =
    {
        "Stack  0", "Stack  1", "Stack  2",
        "Stack  3", "Stack  4", "Stack  5",
        "Stack  6", "Stack  7", "Stack  8",
        "Stack  9", "Stack 10", "Stack 11"
    };

    std::vector<struct iio_stacks_on_socket> iios;
    ASSERT_TRUE(IPlatformMapping::initializeIOStacksStructure(iios, model, sockets, stacks)) << "Failed to initialize IIO stacks structure";

    ASSERT_EQ(iios.size(), sockets) << "Number of sockets mismatch";
    for (const auto &iio_on_socket : iios)
    {
        ASSERT_EQ(iio_on_socket.stacks.size(), stacks) << "Number of stacks per socket mismatch";
        for (uint32_t unit = 0; unit < stacks; unit++)
        {
            ASSERT_EQ(iio_on_socket.stacks[unit].iio_unit_id, unit) << "Stack ID mismatch";
            EXPECT_EQ(iio_on_socket.stacks[unit].stack_name, expectedStackNames[unit]) << "Stack name mismatch";
        }
    }
}

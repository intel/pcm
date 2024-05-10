// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2022, Intel Corporation

#pragma once

#include <vector>
#include <unordered_map>
#include <iostream>
#include "types.h"

namespace pcm {

constexpr auto SPR_PCU_BOX_TYPE = 4U;
constexpr auto SPR_IMC_BOX_TYPE = 6U;
constexpr auto SPR_UPILL_BOX_TYPE = 8U;
constexpr auto SPR_MDF_BOX_TYPE = 11U;
constexpr auto SPR_CXLCM_BOX_TYPE = 12U;
constexpr auto SPR_CXLDP_BOX_TYPE = 13U;
constexpr auto BHS_MDF_BOX_TYPE = 20U;
constexpr auto BHS_PCIE_GEN5x16_TYPE = 21U;
constexpr auto BHS_PCIE_GEN5x8_TYPE = 22U;

class UncorePMUDiscovery
{
public:
    enum accessTypeEnum {
        MSR = 0,
        MMIO = 1,
        PCICFG = 2,
        unknownAccessType = 255
    };
    static const char * accessTypeStr(const uint64 t)
    {
        switch (t)
        {
            case MSR:
                return "MSR";
            case MMIO:
                return "MMIO";
            case PCICFG:
                return "PCICFG";
        }
        return "unknown";
    };
    union PCICFGAddress
    {
        uint64 raw;
        struct {
            uint64 offset:12;
            uint64 function:3;
            uint64 device:5;
            uint64 bus:8;
        } fields;
        std::string getStr() const
        {
            std::ostringstream out(std::ostringstream::out);
            out << std::hex << fields.bus << ":" << fields.device << "." << fields.function << "@" << fields.offset;
            out << std::dec;
            return out.str();
        }
    };
    static void printHelper(const accessTypeEnum accessType, const uint64 addr)
    {
        if (accessType == PCICFG)
        {
            PCICFGAddress Addr;
            Addr.raw = addr;
            std::cout << " (" << Addr.getStr() << ")";
        }
        else
        {
            std::cout << " (-)";
        }
        std::cout << " with access type " << std::dec << accessTypeStr(accessType);
    }
protected:
    struct GlobalPMU
    {
        uint64 type:8;
        uint64 stride:8;
        uint64 maxUnits:10;
        uint64 __reserved1:36;
        uint64 accessType:2;
        uint64 globalCtrlAddr;
        uint64 statusOffset:8;
        uint64 numStatus:16;
        uint64 __reserved2:40;
        void print() const
        {
            std::cout << "global PMU " <<
            " of type " << std::dec << type <<
            " globalCtrl: 0x" << std::hex << globalCtrlAddr;
            UncorePMUDiscovery::printHelper((accessTypeEnum)accessType, globalCtrlAddr);
            std::cout << " stride: " << std::dec <<  stride
            << "\n";
        }
    };
    struct BoxPMU
    {
        uint64 numRegs : 8;
        uint64 ctrlOffset : 8;
        uint64 bitWidth : 8;
        uint64 ctrOffset : 8;
        uint64 statusOffset : 8;
        uint64 __reserved1 : 22;
        uint64 accessType : 2;
        uint64 boxCtrlAddr;
        uint64 boxType : 16;
        uint64 boxID : 16;
        uint64 __reserved2 : 32;
        void print() const
        {
           std::cout << "unit PMU " <<
                " of type " << std::dec <<  boxType <<
                " ID " << boxID <<
                " box ctrl: 0x" << std::hex << boxCtrlAddr;
            UncorePMUDiscovery::printHelper((accessTypeEnum)accessType, boxCtrlAddr);
            std::cout <<
                " width " << bitWidth <<
                " numRegs " << numRegs <<
                " ctrlOffset " << ctrlOffset <<
                " ctrOffset " << ctrOffset <<
                "\n";
        }
    };
    typedef std::vector<BoxPMU> BoxPMUs;
    typedef std::unordered_map<size_t, BoxPMUs> BoxPMUMap; // boxType -> BoxPMUs
    std::vector<BoxPMUMap> boxPMUs;
    std::vector<GlobalPMU> globalPMUs;

    bool validBox(const size_t boxType, const size_t socket, const size_t pos)
    {
        return socket < boxPMUs.size() && pos < boxPMUs[socket][boxType].size();
    }
    size_t registerStep(const size_t boxType, const size_t socket, const size_t pos)
    {
        const auto width = boxPMUs[socket][boxType][pos].bitWidth;
        switch (boxPMUs[socket][boxType][pos].accessType)
        {
        case MSR:
            if (width <= 64)
            {
                return 1;
            }
            break;
        case PCICFG:
        case MMIO:
            if (width <= 8)
            {
                return 1;
            }
            else if (width <= 16)
            {
                return 2;
            }
            else if (width <= 32)
            {
                return 4;
            }
            else if (width <= 64)
            {
                return 8;
            }
            break;
        }
        return 0;
    }
public:
    UncorePMUDiscovery();

    size_t getNumBoxes(const size_t boxType, const size_t socket)
    {
        if (socket < boxPMUs.size())
        {
            return boxPMUs[socket][boxType].size();
        }
        return 0;
    }

    uint64 getBoxCtlAddr(const size_t boxType, const size_t socket, const size_t pos)
    {
        if (validBox(boxType, socket, pos))
        {
            return boxPMUs[socket][boxType][pos].boxCtrlAddr;
        }
        return 0;
    }

    uint64 getBoxCtlAddr(const size_t boxType, const size_t socket, const size_t pos, const size_t c)
    {
        if (validBox(boxType, socket, pos) && c < boxPMUs[socket][boxType][pos].numRegs)
        {
            const size_t step = (boxType == SPR_IMC_BOX_TYPE) ? 4 : registerStep(boxType, socket, pos);
            return boxPMUs[socket][boxType][pos].boxCtrlAddr + boxPMUs[socket][boxType][pos].ctrlOffset + c * step;
        }
        return 0;
    }

    uint64 getBoxCtrAddr(const size_t boxType, const size_t socket, const size_t pos, const size_t c)
    {
        if (validBox(boxType, socket, pos) && c < boxPMUs[socket][boxType][pos].numRegs)
        {
            return boxPMUs[socket][boxType][pos].boxCtrlAddr + boxPMUs[socket][boxType][pos].ctrOffset + c * registerStep(boxType, socket, pos);
        }
        return 0;
    }

    accessTypeEnum getBoxAccessType(const size_t boxType, const size_t socket, const size_t pos)
    {
        if (validBox(boxType, socket, pos))
        {
            return static_cast<accessTypeEnum>(boxPMUs[socket][boxType][pos].accessType);
        }
        return unknownAccessType;
    }

    uint64 getBoxNumRegs(const size_t boxType, const size_t socket, const size_t pos)
    {
        if (validBox(boxType, socket, pos))
        {
            return boxPMUs[socket][boxType][pos].numRegs;
        }
        return 0;
    }
};

} // namespace pcm

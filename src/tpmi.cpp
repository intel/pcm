// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2023, Intel Corporation
// written by Roman Dementiev
//

#include "tpmi.h"
#include "pci.h"
#include "utils.h"
#include <vector>
#include <unordered_map>
#include <assert.h>
#ifdef __linux__
#include <algorithm>
#endif

namespace pcm {

constexpr uint32 TPMIInvalidValue = ~0U;

bool TPMIverbose = false;

class PFSInstances
{
public:
    // [TPMI ID][entry] -> base address
    typedef std::unordered_map<size_t, std::vector<size_t> > PFSMapType;
    // [PFS instance][TPMI ID][entry] -> base address
    typedef std::vector<PFSMapType> PFSInstancesType;
private:
    static std::shared_ptr<PFSInstancesType> PFSInstancesSingleton;
public:
    static PFSInstancesType & get()
    {
        if (PFSInstancesSingleton.get())
        {
            return *PFSInstancesSingleton.get();
        }
        // PFSInstancesSingleton not initialized, let us initialize it
        auto PFSInstancesSingletonInit = std::make_shared<PFSInstancesType>();

        processDVSEC([](const VSEC & vsec)
        {
            return vsec.fields.cap_id == 0xb // Vendor Specific DVSEC
                && vsec.fields.vsec_id == 0x42; // TPMI PM_Features
        }, [&](const uint64 bar, const VSEC & vsec)
        {
            struct PFS
            {
                uint64 TPMI_ID:8;
                uint64 NumEntries:8;
                uint64 EntrySize:16;
                uint64 CapOffset:16;
                uint64 Attribute:2;
                uint64 Reserved:14;
            };
            static_assert(sizeof(PFS) == sizeof(uint64), "sizeof(PFS) != sizeof(uint64)");
            assert(vsec.fields.EntrySize == 2);
            std::vector<PFS> pfsArray(vsec.fields.NumEntries);
            try {
                mmio_memcpy(&(pfsArray[0]), bar + vsec.fields.Address, vsec.fields.NumEntries * sizeof(PFS), true, true);
            } catch (std::runtime_error & e)
            {
                std::cerr << "Can't read PFS\n";
                std::cerr << e.what();
            }
            PFSInstancesSingletonInit->push_back(PFSMapType());
            for (const auto & pfs : pfsArray)
            {
                if (TPMIverbose)
                {
                    std::cout << "PFS" <<
                    "\t TPMI_ID: " << pfs.TPMI_ID <<
                    "\t NumEntries: " << pfs.NumEntries <<
                    "\t EntrySize: " << pfs.EntrySize <<
                    "\t CapOffset: " << pfs.CapOffset <<
                    "\t Attribute: " << pfs.Attribute <<
                    "\n";
                }
                for (uint64 p = 0; p < pfs.NumEntries; ++p)
                {
                    uint32 reg0 = 0;
                    const auto addr = bar + vsec.fields.Address + pfs.CapOffset * 1024ULL + p * pfs.EntrySize * sizeof(uint32);
                    try {
                        mmio_memcpy(&reg0, addr, sizeof(uint32), false, true);
                    } catch (std::runtime_error & e)
                    {
                        if (TPMIverbose)
                        {
                            std::cout << "can't read entry " << p << "\n";
                            std::cout << e.what();
                        }
                        PFSInstancesSingletonInit->back()[pfs.TPMI_ID].push_back(addr);
                        continue;
                    }
                    if (reg0 == TPMIInvalidValue)
                    {
                        if (TPMIverbose)
                        {
                            std::cout << "invalid entry " << p << "\n";
                        }
                    }
                    else
                    {
                        if (TPMIverbose)
                        {
                            std::cout << "Entry "<< p << std::hex;
                            for (uint64 i_offset = 0; i_offset < pfs.EntrySize * sizeof(uint32);  i_offset += sizeof(uint64))
                            {
                                uint64 reg = 0;
                                mmio_memcpy(&reg, addr + i_offset, sizeof(uint64), false);
                                std::cout << " register "<< i_offset << " = " << reg;
                            }
                            std::cout << std::dec << "\n";
                        }
                        PFSInstancesSingletonInit->back()[pfs.TPMI_ID].push_back(addr);
                    }
                }
            }
        });
        PFSInstancesSingleton = PFSInstancesSingletonInit;
        return *PFSInstancesSingleton.get();
    }
};

std::shared_ptr<PFSInstances::PFSInstancesType> PFSInstances::PFSInstancesSingleton;

class TPMIHandleMMIO : public TPMIHandleInterface
{
    TPMIHandleMMIO(const TPMIHandleMMIO&) = delete;
    TPMIHandleMMIO& operator = (const TPMIHandleMMIO&) = delete;
    struct Entry
    {
        std::shared_ptr<MMIORange> range;
        size_t offset;
    };
    std::vector<Entry> entries;
public:
    static size_t getNumInstances();
    static void setVerbose(const bool);
    TPMIHandleMMIO(const size_t instance_, const size_t ID_, const size_t offset_, const bool readonly_ = true);
    size_t getNumEntries() const override
    {
        return entries.size();
    }
    uint64 read64(size_t entryPos) override;
    void write64(size_t entryPos, uint64 val) override;
};

size_t TPMIHandleMMIO::getNumInstances()
{
    return PFSInstances::get().size();
}

void TPMIHandle::setVerbose(const bool v)
{
    TPMIverbose = v;
}

TPMIHandleMMIO::TPMIHandleMMIO(const size_t instance_, const size_t ID_, const size_t requestedRelativeOffset, const bool readonly_)
{
    auto & pfsInstances = PFSInstances::get();
    assert(instance_ < pfsInstances.size());
    for (const auto & addr: pfsInstances[instance_][ID_])
    {
        const auto requestedAddr = addr + requestedRelativeOffset;
        const auto baseAddr = roundDownTo4K(requestedAddr);
        const auto baseOffset = requestedAddr - baseAddr;
        Entry e;
        e.range = std::make_shared<MMIORange>(baseAddr, 4096ULL, readonly_);
        e.offset = baseOffset;
        entries.push_back(e);
    }
}

uint64 TPMIHandleMMIO::read64(size_t entryPos)
{
    assert(entryPos < entries.size());
    return entries[entryPos].range->read64(entries[entryPos].offset);
}

void TPMIHandleMMIO::write64(size_t entryPos, uint64 val)
{
    assert(entryPos < entries.size());
    entries[entryPos].range->write64(entries[entryPos].offset, val);
}

#ifdef __linux__
class TPMIHandleDriver : public TPMIHandleInterface
{
    TPMIHandleDriver(const TPMIHandleDriver&) = delete;
    TPMIHandleDriver& operator = (const TPMIHandleDriver&) = delete;
    static std::vector<std::string> instancePaths;
    typedef std::unordered_map<uint32, std::string> TPMI_IDPathMap;
    static std::vector<TPMI_IDPathMap> AllIDPaths;
    static int available;
    static bool isAvailable();
    const size_t instance;
    const size_t ID;
    const size_t offset;
    // const bool readonly; // not used
    size_t nentries;
    struct TPMIEntry {
        unsigned int offset{0};
        std::vector<uint32> data;
    };

    size_t findValidIndex(const std::vector<TPMIEntry> & entries, const size_t & entryPos)
    {
        size_t validIndex = 0;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            if (entries[i].data.empty() || entries[i].data[0] == TPMIInvalidValue)
            {
                // invalid, skip it
                continue;
            }
            if (validIndex == entryPos)
            {
                // found the right instance
                return i;
            }
            ++validIndex;
        }
        assert(0 && "TPMIHandleDriver: entryPos not found");
        return 0;
    }
    std::vector<TPMIEntry> readTPMIFile(std::string filePath)
    {
        filePath += "/mem_dump";
        std::vector<TPMIEntry> entries;
        std::ifstream file(filePath);
        std::string line;

        if (!file.is_open()) {
            std::cerr << "Error opening file: " << filePath << std::endl;
            return entries;
        }

        TPMIEntry currentEntry;
        while (getline(file, line)) {
            if (line.find("TPMI Instance:") != std::string::npos) {
                // If we have a previous instance, push it back to the vector
                if (!currentEntry.data.empty()) {
                    entries.push_back(currentEntry);
                    currentEntry.data.clear();
                }

                std::istringstream iss(line);
                std::string temp;
                iss >> temp >> temp >> temp; // Skip "TPMI Instance:"
                iss >> temp; // Skip entry number
                iss >> temp >> std::hex >> currentEntry.offset; // Read offset
            } else {
                std::istringstream iss(line);
                std::string address;
                iss >> address; // Skip the address part

                uint32_t value;
                while (iss >> std::hex >> value) {
                    currentEntry.data.push_back(value);
                }
            }
        }

        // Push the last instance if it exists
        if (!currentEntry.data.empty()) {
            entries.push_back(currentEntry);
        }

        return entries;
    }
public:
    static size_t getNumInstances();
    TPMIHandleDriver(const size_t instance_, const size_t ID_, const size_t offset_, const bool /* readonly_ */ = true) :
        instance(instance_),
        ID(ID_),
        offset(offset_),
        // readonly(readonly_), // not used
        nentries(0)
    {
        assert(available > 0);
        assert(instance < getNumInstances());
        const auto entries = readTPMIFile(AllIDPaths[instance][ID]);
        for (auto & e: entries)
        {
            if (e.data.empty() == false && e.data[0] != TPMIInvalidValue)
            {
                // count valid entries
                ++nentries;
            }
        }
    }
    size_t getNumEntries() const override
    {
        assert(available > 0);
        return nentries;
    }
    uint64 read64(size_t entryPos) override
    {
        assert(available > 0);
        assert(instance < getNumInstances());
        const auto entries = readTPMIFile(AllIDPaths[instance][ID]);
        size_t i = findValidIndex(entries, entryPos);
        cvt_ds result;
        const auto i4 = offset / 4;
        assert(i4 + 1 < entries[i].data.size());
        result.ui32.low = entries[i].data[i4];
        result.ui32.high = entries[i].data[i4 + 1];
        return result.ui64;
    }
    void write64(size_t entryPos, uint64 val) override
    {
        assert(available > 0);
        assert(instance < getNumInstances());
        const auto entries = readTPMIFile(AllIDPaths[instance][ID]);
        size_t i = findValidIndex(entries, entryPos);
        cvt_ds out;
        out.ui64 = val;
        const auto path = AllIDPaths[instance][ID] + "/mem_write";
        writeSysFS(path.c_str(), std::to_string(i) + "," + std::to_string(offset) + "," + std::to_string(out.ui32.low));
        writeSysFS(path.c_str(), std::to_string(i) + "," + std::to_string(offset + 4) + "," + std::to_string(out.ui32.high));
    }
};

int TPMIHandleDriver::available = -1;
std::vector<std::string> TPMIHandleDriver::instancePaths;
std::vector<TPMIHandleDriver::TPMI_IDPathMap> TPMIHandleDriver::AllIDPaths;

bool TPMIHandleDriver::isAvailable()
{
    if (available < 0) // not initialized yet
    {
        instancePaths = findPathsFromPattern("/sys/kernel/debug/tpmi-*");
        std::sort(instancePaths.begin(), instancePaths.end());
        for (size_t i = 0; i < instancePaths.size(); ++i)
        {
            // std::cout << instancePaths[i] << std::endl;
            std::string prefix = instancePaths[i] + "/tpmi-id-";
            std::vector<std::string> IDPaths = findPathsFromPattern((prefix + "*").c_str());
            TPMI_IDPathMap idMap;
            for (auto & p : IDPaths)
            {
                const auto id = read_number((std::string("0x") + p.substr(prefix.size())).c_str());
                // std::cout << p << " -> " <<  id << std::endl;
                idMap[id] = p;
                std::ifstream mem_dump((p + "/mem_dump").c_str());
                std::ifstream mem_write((p + "/mem_write").c_str());
                if (mem_dump.good() && mem_write.good())
                {
                    available = 1;
                }
            }
            AllIDPaths.push_back(idMap);
        }
        if (available < 0)
        {
            available = 0;
        }
        if (safe_getenv("PCM_NO_TPMI_DRIVER") == std::string("1"))
        {
            available = 0;
        }
    }
    return available > 0;
}

size_t TPMIHandleDriver::getNumInstances()
{
    // std::cout << "isAvailable: " << isAvailable() << std::endl;
    if (isAvailable())
    {
        return AllIDPaths.size();
    }
    return 0;
}

#endif

size_t TPMIHandle::getNumInstances()
{
    #ifdef __linux__
    const auto tpmiNInstances = TPMIHandleDriver::getNumInstances();
    if (tpmiNInstances)
    {
        return tpmiNInstances;
    }
    #endif
    return TPMIHandleMMIO::getNumInstances();
}

TPMIHandle::TPMIHandle(const size_t instance_, const size_t ID_, const size_t requestedRelativeOffset, const bool readonly_)
{
    #ifdef __linux__
    const auto tpmiNInstances = TPMIHandleDriver::getNumInstances();
    if (tpmiNInstances)
    {
        impl = std::make_shared<TPMIHandleDriver>(instance_, ID_, requestedRelativeOffset, readonly_);
        return;
    }
    #endif
    impl = std::make_shared<TPMIHandleMMIO>(instance_, ID_, requestedRelativeOffset, readonly_);
}

size_t TPMIHandle::getNumEntries() const
{
    assert(impl.get());;
    return impl->getNumEntries();
}

uint64 TPMIHandle::read64(size_t entryPos)
{
    assert(impl.get());
    return impl->read64(entryPos);
}

void TPMIHandle::write64(size_t entryPos, uint64 val)
{
    assert(impl.get());
    impl->write64(entryPos, val);
}


} // namespace pcm

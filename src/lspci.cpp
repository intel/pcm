#include "lspci.h"

namespace pcm {

const ccr_config ccr::skx_ccrs(44, 0xFFULL);
const ccr_config ccr::icx_ccrs(48, 0xFFFULL);

bool operator<(const iio_stack& lh, const iio_stack& rh)
{
    return lh.iio_unit_id < rh.iio_unit_id;
}

bool operator<(const bdf &l, const bdf &r) {
    if (l.domainno < r.domainno)
        return true;
    if (l.domainno > r.domainno)
        return false;
    if (l.busno < r.busno)
        return true;
    if (l.busno > r.busno)
        return false;
    if (l.devno < r.devno)
        return true;
    if (l.devno > r.devno)
        return false;
    if (l.funcno < r.funcno)
        return true;
    if (l.funcno > r.funcno)
        return false;

    return false; // bdf == bdf
}

void probe_capability_pci_express(struct pci *p, uint32_t cap_ptr)
{
    struct cap {
        union {
            struct {
                uint8_t id;
                union {
                    uint8_t next;
                    uint8_t cap_ptr;
                };
                uint16_t junk;
            };
            uint32 dw0;
        };
    } cap;
    uint32 value;
    PciHandleType h(0, p->bdf.busno, p->bdf.devno, p->bdf.funcno);
    h.read32(cap_ptr, &value); //Capability pointer
    cap.dw0 = value;
    if (cap.id != 0x10 && cap.next != 0x00) {
        probe_capability_pci_express(p, cap.cap_ptr);
    } else {
        if (cap.id == 0x10) { // We're in PCI express capability structure
            h.read32(cap_ptr+0x10, &value);
            p->link_info = value;
        } else { /*Finish recursive searching but cannot find PCI express capability structure*/ }
    }
}

bool probe_pci(struct pci *p)
{
    uint32 value;
    p->exist = false;
    struct bdf *bdf = &p->bdf;
    if (PciHandleType::exists(bdf->domainno, bdf->busno, bdf->devno, bdf->funcno)) {
        PciHandleType h(bdf->domainno, bdf->busno, bdf->devno, bdf->funcno);
        // VID:DID
        h.read32(0x0, &value);
        // Invalid VID::DID
        if (value != (std::numeric_limits<unsigned int>::max)()) {
            p->offset_0 = value;
            h.read32(0xc, &value);
            p->header_type = (value >> 16) & 0x7f;
            if (p->header_type == 0) {
                // Status register
                h.read32(0x4, &value);
                // Capability list == true
                if (value & 0x100000) {
                    // Capability pointer
                    h.read32(0x34, &value);
                    probe_capability_pci_express(p, value);
                }
            } else if (p->header_type == 1) {
                h.read32(0x18, &value);
                p->offset_18 = value;
            }
            p->exist = true;
        }
    }

    return p->exist;
}

void load_PCIDB(PCIDB & pciDB)
{
    std::ifstream in(PCI_IDS_PATH);
    std::string line, item;

    if (!in.is_open())
    {
#ifndef _MSC_VER
        // On Unix, try PCI_IDS_PATH2
        in.open(PCI_IDS_PATH2);
    }

    if (!in.is_open())
    {
        // On Unix, try the current directory if the default path failed
        in.open("pci.ids");
    }

    if (!in.is_open())
    {
#endif
        std::cerr << PCI_IDS_NOT_FOUND << "\n";
        return;
    }

    int vendorID = -1;

    while (std::getline(in, line)) {
        // Ignore any line starting with #
        if (line.size() == 0 || line[0] == '#')
            continue;

        if (line[0] == '\t' && line[1] == '\t')
        {
            // subvendor subdevice  subsystem_name
            continue;
        }
        if (line[0] == '\t')
        {
            int deviceID = stoi(line.substr(1,4),0,16);
            //std::cout << vendorID << ";" << vendorName << ";" << deviceID << ";" << line.substr(7) << "\n";
            pciDB.second[vendorID][deviceID] = line.substr(7);
            continue;
        }
        // vendor
        vendorID = stoi(line.substr(0,4),0,16);
        pciDB.first[vendorID] = line.substr(6);
    }
}

} // Namespace pcm

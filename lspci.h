#ifndef CPUCounters_LSPCI_H
#define CPUCounters_LSPCI_H

#include <vector>
#include <fstream>
#include "cpucounters.h"

#if defined(_MSC_VER)
#define PCI_IDS_PATH "pci.ids"
#define PCI_IDS_NOT_FOUND "pci.ids file is not available. Download it from" \
    " https://raw.githubusercontent.com/pciutils/pciids/master/pci.ids."
#elif defined (__FreeBSD__) || defined(__DragonFly__)
#define PCI_IDS_PATH "/usr/local/share/pciids/pci.ids"
#define PCI_IDS_NOT_FOUND "/usr/local/share/pciids/pci.ids file is not available." \
    " Ensure that the \"pciids\" package is properly installed or download" \
    " https://raw.githubusercontent.com/pciutils/pciids/master/pci.ids and" \
    " copy it to the current directory."
#else
#define PCI_IDS_PATH "/usr/share/hwdata/pci.ids"
#define PCI_IDS_NOT_FOUND "/usr/share/hwdata/pci.ids file is not available." \
    " Ensure that the \"hwdata\" package is properly installed or download" \
    " https://raw.githubusercontent.com/pciutils/pciids/master/pci.ids and" \
    " copy it to the current directory."
#endif

namespace pcm {

typedef uint32_t h_id;
typedef uint32_t v_id;
typedef std::map<std::pair<h_id,v_id>,uint64_t> ctr_data;
typedef std::vector<ctr_data> stack_content;
typedef std::vector<stack_content> result_content;

struct bdf {
    uint8_t busno;
    uint8_t devno;
    uint8_t funcno;
};

struct pci {
    bool exist = false;
    struct bdf bdf;
    union {
        struct {
            uint16_t vendor_id;
            uint16_t device_id;
        };
        uint32_t offset_0;
    };
    int8_t header_type;
    union {
        struct {
            uint8_t primary_bus_number;
            uint8_t secondary_bus_number;
            uint8_t subordinate_bus_number;
            uint8_t junk;
        };
        uint32_t offset_18;
    };
    union {
        struct {
            uint16_t link_ctrl;
            union {
                struct {
                    uint16_t link_speed : 4;
                    uint16_t link_width : 6;
                    uint16_t undefined : 1;
                    uint16_t link_trained : 1;
                };
                uint16_t link_sta;
            };
        };
        uint32_t link_info;
    };
};

struct counter {
  std::string h_event_name;
  std::string v_event_name;
    IIOPMUCNTCTLRegister Opcodes;
    int idx; /* Some counters need to be placed in specific index */
    int multiplier;
    int divider;
    uint32_t h_id;
    uint32_t v_id;
    std::vector<result_content> data;
};

struct iio_skx {
    struct {
        struct {
            struct pci root_pci_dev;   /* single device represent root port */
            std::vector<struct pci> child_pci_devs; /* Contain child switch and end-point devices */
        } parts[4]; /* part 0, 1, 2, 3 */
        uint8_t busno; /* holding busno for each IIO stack */
        std::string stack_name;
        std::vector<uint64_t> values;
    } stacks[6]; /* iio stack 0, 1, 2, 3, 4, 5 */
    uint32_t socket_id;
};

bool operator < (const bdf &l, const bdf &r) {
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
};

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

void probe_pci(struct pci *p)
{
    uint32 value;
    struct bdf *bdf = &p->bdf;
    if (PciHandleType::exists(0, bdf->busno, bdf->devno, bdf->funcno)) {
        p->exist = true;
        PciHandleType h(0, bdf->busno, bdf->devno, bdf->funcno);
        h.read32(0x0, &value); //VID:DID
        if (value == (std::numeric_limits<unsigned int>::max)()) // invalid VID::DID
        {
            p->exist = false;
            return;
        }
        p->offset_0 = value;
        h.read32(0xc, &value);
        p->header_type = (value >> 16) & 0x7f;
        if (p->header_type == 0) {
            h.read32(0x4, &value); //Status register
            if (value & 0x100000) {//Capability list == true
                h.read32(0x34, &value); //Capability pointer
                probe_capability_pci_express(p, value);
            }
        } else if (p->header_type == 1) {
            h.read32(0x18, &value);
            p->offset_18 = value;
        }
    }
    else
        p->exist = false;
}

/*
  first : [vendorID] -> vencor name
  second : [vendorID][deviceID] -> device name
 */
typedef std::pair< std::map<int, std::string> ,std::map< int, std::map<int, std::string> > > PCIDB;

void print_pci(struct pci p, const PCIDB & pciDB)
{
    printf("Parent bridge info:");
    printf("%x:%x.%d [%04x:%04x] %s %s %d P:%x S:%x S:%x ",
            p.bdf.busno, p.bdf.devno, p.bdf.funcno,
            p.vendor_id, p.device_id,
            (pciDB.first.count(p.vendor_id) > 0)?pciDB.first.at(p.vendor_id).c_str():"unknown vendor",
            (pciDB.second.count(p.vendor_id) > 0 && pciDB.second.at(p.vendor_id).count(p.device_id) > 0)?pciDB.second.at(p.vendor_id).at(p.device_id).c_str():"unknown device",
            p.header_type,
            p.primary_bus_number, p.secondary_bus_number, p.subordinate_bus_number);
    printf("Device info:");
    printf("%x:%x.%d [%04x:%04x] %s %s %d Gen%d x%d\n",
            p.bdf.busno, p.bdf.devno, p.bdf.funcno,
            p.vendor_id, p.device_id,
            (pciDB.first.count(p.vendor_id) > 0)?pciDB.first.at(p.vendor_id).c_str():"unknown vendor",
            (pciDB.second.count(p.vendor_id) > 0 && pciDB.second.at(p.vendor_id).count(p.device_id) > 0)?pciDB.second.at(p.vendor_id).at(p.device_id).c_str():"unknown device",
            p.header_type,
            p.link_speed, p.link_width);
}

void load_PCIDB(PCIDB & pciDB)
{
    std::ifstream in(PCI_IDS_PATH);
    std::string line, item;

    if (!in.is_open())
    {
#ifndef _MSC_VER
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

} // namespace pcm

#endif

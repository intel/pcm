#ifndef CPUCounters_LSPCI_H
#define CPUCounters_LSPCI_H

#include <vector>
#include <fstream>
#include <memory>
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
// different distributions put it in different places
#define PCI_IDS_PATH "/usr/share/hwdata/pci.ids"
#define PCI_IDS_PATH2 "/usr/share/misc/pci.ids"
#define PCI_IDS_NOT_FOUND "/usr/share/hwdata/pci.ids file is not available." \
    " Ensure that the \"hwdata\" package is properly installed or download" \
    " https://raw.githubusercontent.com/pciutils/pciids/master/pci.ids and" \
    " copy it to the current directory."
#endif
#define PCI_IDS_PATH2 "/usr/share/misc/pci.ids"

namespace pcm {

typedef uint32_t h_id;
typedef uint32_t v_id;
typedef std::map<std::pair<h_id,v_id>,uint64_t> ctr_data;
typedef std::vector<ctr_data> stack_content;
typedef std::vector<stack_content> result_content;

struct ccr_config {
    static constexpr uint64_t EVENT_SELECT_MASK = 0xFFULL;
    static constexpr uint8_t  UMASK_SHIFT = 8;
    static constexpr uint64_t UMASK_MASK = 0xFFULL << UMASK_SHIFT;
    static constexpr uint8_t  RESET_SHIFT = 17;
    static constexpr uint64_t RESET_MASK = 0x01ULL << RESET_SHIFT;
    static constexpr uint8_t  EDGE_SHIFT = 18;
    static constexpr uint64_t EDGE_MASK = 0x01ULL << EDGE_SHIFT;
    static constexpr uint8_t  OV_EN_SHIFT = 20;
    static constexpr uint64_t OV_EN_MASK = 0x01ULL << OV_EN_SHIFT;
    static constexpr uint8_t  ENABLE_SHIFT = 22;
    static constexpr uint64_t ENABLE_MASK = 0x01ULL << ENABLE_SHIFT;
    static constexpr uint8_t  INVERT_SHIFT = 23;
    static constexpr uint64_t INVERT_MASK = 0x01ULL << INVERT_SHIFT;
    static constexpr uint8_t  THRESH_SHIFT = 24;
    static constexpr uint64_t THRESH_MASK = 0xFFFULL << THRESH_SHIFT;
    static constexpr uint8_t  CH_MASK_SHIFT = 36;

    uint8_t  FC_MASK_SHIFT;
    uint64_t FC_MASK;
    uint64_t CH_MASK;

    ccr_config(uint8_t fc_mask_shift, uint64_t ch_mask)
    : FC_MASK_SHIFT(fc_mask_shift), FC_MASK(0x07ULL << fc_mask_shift), CH_MASK(ch_mask << CH_MASK_SHIFT) {}
};

class ccr {
public:
    enum class ccr_type {
        skx,
        icx
    };

    ccr(uint64_t &v, const ccr_type &type) : ccr_value(v), config(type == ccr_type::skx ? skx_ccrs : icx_ccrs) { }

    ccr() = delete;

    ~ccr() = default;

    uint64_t get_event_select() const {
        return (ccr_value & config.EVENT_SELECT_MASK);
    }

    void set_event_select(uint64_t value) {
        ccr_value = (ccr_value & ~config.EVENT_SELECT_MASK) | (value & config.EVENT_SELECT_MASK);
    }

    uint64_t get_umask() const {
        return (ccr_value & config.UMASK_MASK) >> config.UMASK_SHIFT;
    }

    void set_umask(uint64_t value) {
        ccr_value = (ccr_value & ~config.UMASK_MASK) | ((value << config.UMASK_SHIFT) & config.UMASK_MASK);
    }

    uint64_t get_reset() const {
        return (ccr_value & config.RESET_MASK) >> config.RESET_SHIFT;
    }

    void set_reset(uint64_t value) {
        ccr_value = (ccr_value & ~config.RESET_MASK) | ((value << config.RESET_SHIFT) & config.RESET_MASK);
    }

    uint64_t get_edge() const {
        return (ccr_value & config.EDGE_MASK) >> config.EDGE_SHIFT;
    }

    void set_edge(uint64_t value) {
        ccr_value = (ccr_value & ~config.EDGE_MASK) | ((value << config.EDGE_SHIFT) & config.EDGE_MASK);
    }

    uint64_t get_ov_en() const {
        return (ccr_value & config.OV_EN_MASK) >> config.OV_EN_SHIFT;
    }

    void set_ov_en(uint64_t value) {
        ccr_value = (ccr_value & ~config.OV_EN_MASK) | ((value << config.OV_EN_SHIFT) & config.OV_EN_MASK);
    }

    uint64_t get_enable() const {
        return (ccr_value & config.ENABLE_MASK) >> config.ENABLE_SHIFT;
    }

    void set_enable(uint64_t value) {
        ccr_value = (ccr_value & ~config.ENABLE_MASK) | ((value << config.ENABLE_SHIFT) & config.ENABLE_MASK);
    }

    uint64_t get_invert() const {
        return (ccr_value & config.INVERT_MASK) >> config.INVERT_SHIFT;
    }

    void set_invert(uint64_t value) {
        ccr_value = (ccr_value & ~config.INVERT_MASK) | ((value << config.INVERT_SHIFT) & config.INVERT_MASK);
    }

    uint64_t get_thresh() const {
        return (ccr_value & config.THRESH_MASK) >> config.THRESH_SHIFT;
    }

    void set_thresh(uint64_t value) {
        ccr_value = (ccr_value & ~config.THRESH_MASK) | ((value << config.THRESH_SHIFT) & config.THRESH_MASK);
    }

    uint64_t get_ch_mask() const {
        return (ccr_value & config.CH_MASK) >> config.CH_MASK_SHIFT;
    }

    void set_ch_mask(uint64_t value) {
        ccr_value = (ccr_value & ~config.CH_MASK) | ((value << config.CH_MASK_SHIFT) & config.CH_MASK);
    }

    uint64_t get_fc_mask() const {
        return (ccr_value & config.FC_MASK) >> config.FC_MASK_SHIFT;
    }

    void set_fc_mask(uint64_t value) {
        ccr_value = (ccr_value & ~config.FC_MASK) | ((value << config.FC_MASK_SHIFT) & config.FC_MASK);
    }

    uint64_t get_ccr_value() const { return ccr_value; }

    void set_ccr_value(uint64_t value) { ccr_value = value; }
private:
    uint64_t &ccr_value;
    const ccr_config &config;

    static const ccr_config skx_ccrs;
    static const ccr_config icx_ccrs;
};

const ccr_config ccr::skx_ccrs(44, 0xFFULL);
const ccr_config ccr::icx_ccrs(48, 0xFFFULL);

struct bdf {
    uint32_t domainno;
    uint8_t busno;
    uint8_t devno;
    uint8_t funcno;
    bdf() : domainno(0), busno(0), devno(0), funcno(0) {}
    bdf(uint32_t domain, uint8_t bus, uint8_t device, uint8_t function) :
        domainno(domain), busno(bus), devno(device), funcno(function) {}
    bdf(uint8_t bus, uint8_t device, uint8_t function) :
        domainno(0), busno(bus), devno(device), funcno(function) {}
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

    std::vector<uint8_t> parts_no;
    std::vector<struct pci> child_pci_devs;

    pci() : exist(false), offset_0(0), header_type(0),
            offset_18(0), link_info(0), parts_no{},
            child_pci_devs{}
            {}

    pci(uint32_t domain, uint8_t bus, uint8_t device, uint8_t function) :
        exist(false), bdf(domain, bus, device, function), offset_0(0), header_type(0),
        offset_18(0), link_info(0), parts_no{}, child_pci_devs{}
        {}

    pci(uint8_t bus, uint8_t device, uint8_t function) :
        exist(false), bdf(bus, device, function), offset_0(0), header_type(0),
        offset_18(0), link_info(0), parts_no{}, child_pci_devs{}
        {}

    pci(const struct bdf &address) : exist(false), bdf(address), offset_0(0), header_type(0),
        offset_18(0), link_info(0), parts_no{}, child_pci_devs{}
        {}

    bool hasChildDevices() const { return (child_pci_devs.size() != 0); }

    bool isIntelDevice() const { return (vendor_id == PCM_INTEL_PCI_VENDOR_ID); }
};
struct iio_skx {
    struct {
        struct {
            struct pci root_pci_dev;   /* single device represent root port */
            std::vector<struct pci> child_pci_devs; /* Contain child switch and end-point devices */
        } parts[4]{}; /* part 0, 1, 2, 3 */
        uint8_t busno{}; /* holding busno for each IIO stack */
        std::string stack_name{};
        std::vector<uint64_t> values{};
        bool flipped = false;
    } stacks[6]; /* iio stack 0, 1, 2, 3, 4, 5 */
    uint32_t socket_id{};
};

struct iio_bifurcated_part {
    int part_id{0};
    /* single device represent root port */
    struct pci root_pci_dev;
    /* Contain child switch and end-point devices */
    std::vector<struct pci> child_pci_devs;
};

struct iio_stack {
    std::vector<struct iio_bifurcated_part> parts{};
    uint32_t iio_unit_id{};
    std::string stack_name{};
    std::vector<uint64_t> values{};
    bool flipped = false;
    /* holding busno for each IIO stack */
    uint32_t domain{};
    uint8_t busno{};

    iio_stack() : iio_unit_id(0), stack_name(""), domain(0), busno(0) {}
};

bool operator<(const iio_stack& lh, const iio_stack& rh)
{
    return lh.iio_unit_id < rh.iio_unit_id;
}

struct iio_stacks_on_socket {
    std::vector<struct iio_stack> stacks{};
    uint32_t socket_id{};
};

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

} // namespace pcm

#endif

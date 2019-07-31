/*!     \file whitelist.c
        \brief Whitelist MSR and PCI access to pre-approved registers
*/

#include "whitelist.h"

bool AllowMSRAccess(uint64_t msrAddress)
{
    // Test broad range first (by range size in hope of faster processing)
    if ((0xC00 <= msrAddress && 0xEFF >= msrAddress) ||
        (0x3B0 <= msrAddress && 0x3CF >= msrAddress) ||
        (0x702 <= msrAddress && 0x71A >= msrAddress) ||
        (0x606 <= msrAddress && 0x619 >= msrAddress) ||
        (0xC1 <= msrAddress && 0xCE >= msrAddress) ||
        (0x38D <= msrAddress && 0x39C >= msrAddress) ||
        (0x3F8 <= msrAddress && 0x3FF >= msrAddress) ||
        (0xA40 <= msrAddress && 0xA47 >= msrAddress) ||
        (0x10A <= msrAddress && 0x10F >= msrAddress) ||
        (0x186 <= msrAddress && 0x189 >= msrAddress) ||
        (0x309 <= msrAddress && 0x30B >= msrAddress) ||
        (0x630 <= msrAddress && 0x632 >= msrAddress)) return true;

    // Check explicit registers in areas with more sensitive registers available
    if (0x10 == msrAddress || 0x20 == msrAddress || 0x34 == msrAddress ||
        0x48 == msrAddress || 0x8B == msrAddress || 0x19C == msrAddress ||
        0x1A6 == msrAddress || 0x1A7 ==msrAddress || 0x1B1 == msrAddress || 
        0x1D9 == msrAddress) return true;

    return false;
}

bool AllowPCICFGAccess(uint32_t device, uint32_t offset)
{
    // Check for special cases first
    if ((device == 0 && offset == 0x48) ||
        (device == 5 && offset == 0x108) ||
        (device == 8 && offset == 0x0) ||
        (device == 9 && offset == 0x0) ||
        (device == 16 && offset == 0x0) ||
        (device == 24 && offset == 0x0) ||
        (device == 30 && offset == 0x0)) return true;

    // Check device not outside range
    if (!(
        (8 <= device && 16 >= device) ||
        (20 <= device && 32 >= device)
        )) return false;

    //Check offset
    if ((0x80 <= offset && 0x84 >= offset) ||
        (0xA0 <= offset && 0xFF >= offset) ||
        (0x200 <= offset && 0x25F >= offset) ||
        (0x318 <= offset && 0x37F >= offset) ||
        (0xA00 <= offset && 0xA4F >= offset) ||
        (0xB00 <= offset && 0xB4F >= offset)) return true;

    return false;
}

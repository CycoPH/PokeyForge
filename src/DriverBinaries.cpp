#include "DriverBinaries.h"

#include "Atari.h"

#include <cstdio>
#include <fstream>
#include <vector>

namespace DriverBinaries {

bool LoadIntoAtari(Atari& atari, const char* obx_path)
{
    std::ifstream in(obx_path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "DriverBinaries: cannot open '%s'\n", obx_path);
        return false;
    }
    in.seekg(0, std::ios::end);
    auto size = (size_t)in.tellg();
    in.seekg(0, std::ios::beg);

    std::vector<byte> buf(size);
    if (size) in.read(reinterpret_cast<char*>(buf.data()), size);

    size_t p = 0;
    int segments = 0;
    while (p + 4 <= size) {
        std::uint16_t bfrom = (std::uint16_t)buf[p] | (std::uint16_t)(buf[p + 1] << 8);
        p += 2;
        if (bfrom == 0xFFFF) {
            // Atari binary file header marker (may also reappear between
            // segments). Skip and re-read the start address.
            if (p + 2 > size) break;
            bfrom = (std::uint16_t)buf[p] | (std::uint16_t)(buf[p + 1] << 8);
            p += 2;
        }
        if (p + 2 > size) break;
        std::uint16_t bto = (std::uint16_t)buf[p] | (std::uint16_t)(buf[p + 1] << 8);
        p += 2;

        if (bto < bfrom) {
            std::fprintf(stderr, "DriverBinaries: malformed segment %d..%d\n",
                         bfrom, bto);
            return false;
        }
        size_t blen = (size_t)(bto - bfrom) + 1;
        if (p + blen > size) {
            std::fprintf(stderr, "DriverBinaries: truncated segment\n");
            return false;
        }

        byte* dst = atari.GetMemoryAt(bfrom);
        std::memcpy(dst, buf.data() + p, blen);
        p += blen;
        ++segments;
    }

    if (segments == 0) {
        std::fprintf(stderr, "DriverBinaries: no segments loaded from '%s'\n",
                     obx_path);
        return false;
    }
    return true;
}

} // namespace DriverBinaries

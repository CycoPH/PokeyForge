#pragma once

#include "Types.h"

class Atari;

// Loads an RMT tracker driver .obx (Atari multi-segment binary) into the
// emulated Atari's 64 KB memory. Each segment has a 4-byte header (start,
// end addresses, little-endian) followed by (end - start + 1) bytes of code.
// The first segment may be preceded by the FFFF marker.

namespace DriverBinaries {

// Default driver shipped in PokeyForge/runtime/ is rmt_driver_v2.obx, which is
// the UNPATCHED_WITH_TUNING variant in RMT's TrackerDriverVersion enum.
bool LoadIntoAtari(Atari& atari, const char* obx_path);

} // namespace DriverBinaries

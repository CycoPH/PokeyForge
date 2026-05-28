#pragma once

#include "InstrumentTypes.h"
#include "Types.h"

#include <cstdint>
#include <string>
#include <vector>

class Directory;

// Instrument analysis: hashes each instrument's ATA blob to find sonic
// duplicates and classifies it into a coarse category. Results are applied to
// the Directory (per-file category + duplicate flag) and cached in
// analysis.json in the library folder.

namespace Analysis {

enum class Category { Bass = 0, Lead, Percussion, NoiseFX, Pad, Other, COUNT };

const char* Name(Category c);
std::vector<std::string> Names();   // category names in index order

// 64-bit FNV-1a hash over the ATA blob (the actual instrument definition).
std::uint64_t HashAta(const std::vector<byte>& ata);

// Coarse classification from the decoded instrument's parameters/envelope.
Category Classify(const TInstrument& ins);

struct Summary { int total = 0; int duplicates = 0; bool ok = false; };

// Scan every file in `dir`, classify, detect duplicates, apply the results to
// `dir` (category + is_duplicate per file node, plus category names), rebuild
// its views, and optionally write analysis.json into `libraryRoot`.
Summary Run(Directory& dir, const std::string& libraryRoot, bool writeJson);

// Load analysis.json from `libraryRoot` and apply it to `dir` (matching by
// path). Returns true if a usable file was found and applied.
bool LoadAndApply(Directory& dir, const std::string& libraryRoot);

} // namespace Analysis

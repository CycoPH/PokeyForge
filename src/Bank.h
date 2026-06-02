#pragma once

#include "RtiFile.h"
#include "Types.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// A 64-slot bank of curated instruments. Add() stores the .RTI's name and
// ATA blob; SaveTo() writes each non-empty slot as `NN_name.rti` in the
// output directory plus a `manifest.txt` mapping slot -> source path.
//
// The on-disk format matches RMT's SaveInstrument(InstrumentIOType::RTI),
// so the resulting files load cleanly via RMT's File > Load Instrument.
//
// Slot lifecycle:
//   used=false   - empty slot; ata/name are default-constructed
//   used=true,
//   dirty=false  - mirrors what's on disk (saved or just loaded)
//   used=true,
//   dirty=true   - added/edited since last save; UI tints it orange.
// MarkAllClean() flips dirty=false on every used slot after a successful
// SaveTo / SaveRmt. Remove(slot) resets the slot to a fresh Slot{}, so
// IsFull() and UsedCount() update automatically.
//
// Deduplication helpers (IndexOfPath/IndexOfAta) are pure lookups used by
// the add code paths in main.cpp to avoid creating two slots that point to
// the same source file (IndexOfPath) or hold the same sound info
// (IndexOfAta, which is the strict comparison used for "+", right-click
// add, and the bank-menu Import action).

class Bank {
public:
    static constexpr int SLOT_COUNT = 64;

    struct Slot {
        bool        used = false;
        bool        dirty = false;   // added/edited since last save to disk
        std::string source_path;
        std::string name;
        int         version = 1;
        std::vector<byte> ata;
        // Cached cluster fingerprint set by App::AnalyseAllBankSlots /
        // App::FindClusterForBankSlot. cluster_hash is the FNV-1a of `ata`
        // at the moment cluster_info was computed - so a later edit /
        // import / paste auto-invalidates the cache (HashAta(ata) won't
        // match cluster_hash anymore). Empty cluster_info means "never
        // analysed" and renders as "None" in the bank menu.
        std::string   cluster_info;
        std::uint64_t cluster_hash = 0;
    };

    Bank();

    int  FirstEmptySlot() const;     // -1 if full
    int  IndexOfPath(const std::string& path) const;  // -1 if not present

    // Find the first slot whose stored ATA blob byte-matches `ata` (i.e.,
    // sound-identical, ignoring name/source). -1 if no match.
    int  IndexOfAta(const std::vector<byte>& ata) const;

    // Add to the first empty slot. Returns the slot index, or -1 if the
    // bank is full or the file isn't valid.
    int  Add(const RtiFile& rti);

    // Add to a specific slot; overwrites whatever was there.
    bool AddAt(int slot, const RtiFile& rti);

    // Store a raw working instrument (edited copy) into a slot or the first
    // free slot. Marks the slot dirty. AddWorking returns the slot or -1.
    int  AddWorking(const std::string& name, const std::vector<byte>& ata,
                    const std::string& source_path);
    void SetSlot(int slot, const std::string& name, const std::vector<byte>& ata,
                 const std::string& source_path);

    // Clear the dirty flag on all slots (after a successful save to disk).
    void MarkAllClean();
    bool HasDirty() const;

    // Remove the entry at `slot` (or by source path).
    void Remove(int slot);
    bool RemoveByPath(const std::string& path);

    void Clear();

    const Slot& At(int slot) const { return m_slots[slot]; }
    int         UsedCount()  const;
    bool        IsFull()     const;

    // Set the cached cluster fingerprint on a slot. `info` is the multi-line
    // string displayed in the bank's right-click menu; `hash` is the FNV-1a
    // of the slot's current ATA (so we can detect a stale cache when the
    // slot is mutated later). Empty `info` clears the cache.
    void        SetClusterInfo(int slot, const std::string& info,
                               std::uint64_t hash);

    // Write all non-empty slots to outdir as `NN_name.rti` + manifest.txt.
    // Creates outdir if needed. Returns the number of files written, or
    // -1 on error.
    int  SaveTo(const std::string& outdir) const;

    // Write all non-empty slots into a single RMT module file (silent looping
    // song + the instrument table). Loadable in RMT. Returns true on success.
    bool SaveRmt(const std::string& rmt_path) const;

    // Repopulate the bank from a previously-saved bank folder (reads
    // manifest.txt and the referenced .RTI files). Clears the bank first.
    // Returns the number of slots loaded, or -1 on error.
    int  LoadFromManifest(const std::string& folder_or_manifest);

    // Repopulate the bank from an RMT module's instrument table. Clears the
    // bank first. Returns the number of instruments loaded, or -1 on error.
    int  LoadFromRmt(const std::string& rmt_path);

    // Highest used slot index + 1 (0 if empty). Used as the instrument count
    // when exporting.
    int  HighestUsedPlusOne() const;

private:
    std::array<Slot, SLOT_COUNT> m_slots;
};

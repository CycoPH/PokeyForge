#pragma once

#include "RtiFile.h"
#include "Types.h"

#include <array>
#include <string>
#include <vector>

// A 64-slot bank of curated instruments. Add() stores the .RTI's name and
// ATA blob; SaveTo() writes each non-empty slot as `NN_name.rti` in the
// output directory plus a `manifest.txt` mapping slot -> source path.
//
// The on-disk format matches RMT's SaveInstrument(InstrumentIOType::RTI),
// so the resulting files load cleanly via RMT's File > Load Instrument.

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
    };

    Bank();

    int  FirstEmptySlot() const;     // -1 if full
    int  IndexOfPath(const std::string& path) const;  // -1 if not present

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

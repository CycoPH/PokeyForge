#pragma once

#include "Types.h"

// Jump-table entry points exported by the RMT 6502 tracker driver
// (rmt_driver_v*.obx). Addresses are stable across driver versions because
// they live in a jump table at the start of the driver image.

static constexpr MemoryAddress RMTPLAYR_RASTERMUSICTRACKER = 0x3400;

static constexpr MemoryAddress RMT_INIT     = RMTPLAYR_RASTERMUSICTRACKER + 0;
static constexpr MemoryAddress RMT_PLAY     = RMTPLAYR_RASTERMUSICTRACKER + 3;
static constexpr MemoryAddress RMT_P3       = RMTPLAYR_RASTERMUSICTRACKER + 6;
static constexpr MemoryAddress RMT_SILENCE  = RMTPLAYR_RASTERMUSICTRACKER + 9;
static constexpr MemoryAddress RMT_SETPOKEY = RMTPLAYR_RASTERMUSICTRACKER + 12;

static constexpr MemoryAddress RMT_ATA_SETNOTEINSTR = 0x3D00;
static constexpr MemoryAddress RMT_ATA_SETVOLUME    = 0x3E00;
static constexpr MemoryAddress RMT_ATA_INSTROFF     = 0x3E80;
static constexpr MemoryAddress RMT_ATA_DRIVERVERSION = 0x3E90;

// Instruments live at $4000 + slot * 256.
static constexpr MemoryAddress INSTRUMENTS_BASE = 0x4000;
static constexpr int INSTRUMENT_SLOT_SIZE      = 256;

// POKEY register window (mirrored / forwarded by the tracker driver).
static constexpr MemoryAddress POKEY_BASE = 0xD200;

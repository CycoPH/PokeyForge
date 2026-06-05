#pragma once

#include <string>

// Persistent settings stored as playrti.json next to the executable.
// Schema (flat object):
//   { "library": "...", "last_bank": "...", "last_file": <int> }

struct Config {
    std::string library;     // Last instrument-library root folder
    std::string last_bank;   // Last saved/loaded bank path (.rmt or folder)
    int         last_file = 0; // Index into Directory::AllFiles()
    // F12 audio-path toggle. true = tap mode (drain POKEY float stream
    // into SDL), false = native mode (let the DLL play through its own
    // audio device, RMT-style). See Audio::SetUseAudioTap.
    bool        audio_tap = true;

    // Load from playrti.json beside the exe. Returns false if absent/unreadable
    // (fields keep their defaults).
    bool Load();

    // Write playrti.json beside the exe. Returns false on write failure.
    bool Save() const;

    // Absolute path of the config file (next to the exe).
    static std::string Path();
};

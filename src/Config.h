#pragma once

#include <string>

// Persistent settings stored as playrti.json next to the executable.
// Schema (flat object):
//   { "library": "...", "last_bank": "...", "last_file": <int> }

struct Config {
    std::string library;     // Last instrument-library root folder
    std::string last_bank;   // Last saved/loaded bank path (.rmt or folder)
    int         last_file = 0; // Index into Directory::AllFiles()

    // Load from playrti.json beside the exe. Returns false if absent/unreadable
    // (fields keep their defaults).
    bool Load();

    // Write playrti.json beside the exe. Returns false on write failure.
    bool Save() const;

    // Absolute path of the config file (next to the exe).
    static std::string Path();
};

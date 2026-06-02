# sa_pokey-src

Vendored AltirraSDL source for rebuilding the patched `sa_pokey.dll` that
PokeyForge's analysis pipeline depends on. PokeyForge needs three extra
exports on top of the stock RMT POKEY plugin:

| Export                          | Purpose                                                              |
| ------------------------------- | -------------------------------------------------------------------- |
| `Pokey_GetAnalysisAbiVersion()` | Returns `(major << 16) | minor`; non-zero means tap exports present. |
| `Pokey_SetAudioTap(fn, user)`   | Install a C callback that receives raw POKEY float samples (~64 kHz).|
| `Pokey_SetMute(mute)`           | Silence the native audio output during offline analysis.             |

Without these, `Pokey_Process` always memsets its output buffer to `0x80`
(silence) and routes audio out through a private native device — which
is fine for real-time RMT playback but useless for offline feature
extraction. See [`patches/rmtinterface.cpp.patch`](patches/rmtinterface.cpp.patch)
for the exact source delta against upstream.

## Rebuilding

```pwsh
# From a Developer PowerShell or any shell where MSBuild is reachable:
.\build.ps1
```

This builds four MSBuild projects in dependency order
(`system → ATCore → ATAudio → AltirraRMTPOKEY`), produces
`out\ReleaseAMD64\sa_pokey.dll`, verifies the three extension exports
with `dumpbin /EXPORTS`, and copies the DLL to `..\..\runtime\sa_pokey.dll`
so the next PokeyForge build picks it up.

Useful flags:

| Flag                    | Effect                                                                       |
| ----------------------- | ---------------------------------------------------------------------------- |
| `-Configuration Debug`  | Unoptimised build with full symbols.                                         |
| `-Platform ARM64`       | Build for Windows on ARM (untested but should work).                         |
| `-InstallTo ''`         | Leave the DLL under `out\...\`; don't copy it into the runtime tree.         |
| `-NoVerify`             | Skip the dumpbin export check (a few seconds faster on iterate builds).      |

## What's in here

```
sa_pokey-src/
├── README.md                Sources + license + how to rebuild
├── Copying / Copying.RMT    Upstream licenses (see "Licensing" below)
├── build.ps1                One-button rebuild + install
├── patches/
│   └── rmtinterface.cpp.patch    Unified diff vs upstream HEAD
└── src/
    ├── AltirraRMT.sln       Solution file (configs only expose x86, but
    │                         build.ps1 calls each .vcxproj directly with
    │                         /p:Platform=x64 so the x64 configs hidden in
    │                         the .vcxprojs get exercised)
    ├── AltirraRMTPOKEY/     The actual DLL project — patched
    ├── ATAudio/             POKEY emulator + audio mixer + audio output
    ├── ATCore/              Scheduler, FFT, save-state, RNG, etc.
    ├── system/              Avery Lee's VirtualDub system library (zlib)
    ├── Build/               MSBuild .props sheets used by every project
    └── h/                   Public headers
```

Total source size: ~3 MB. Build outputs (`out\`, `lib\`, `obj\` next to
`src\`) are excluded from PokeyForge's git via `.gitignore`.

## Upstream

This is a snapshot of the AltirraSDL source tree from
[github.com/ilmenit/AltirraSDL](https://github.com/ilmenit/AltirraSDL),
which is itself a port of Avery Lee's [Altirra](https://virtualdub.org/altirra.html)
emulator with an SDL3 frontend. The patched files marked with
`// Extension for PokeyForge` are downstream additions; everything else
is unmodified upstream.

To refresh against a newer upstream:

1. Clone or update [AltirraSDL](https://github.com/ilmenit/AltirraSDL).
2. Re-copy the same six directories listed in the tree above plus
   `AltirraRMT.sln` from the upstream `src/`.
3. Re-apply `patches/rmtinterface.cpp.patch` (or re-do the edit by hand —
   it's only ~50 lines).
4. Re-run `build.ps1` and confirm the smoke test still passes
   (`PokeyForge.exe --smoke-tap smoke.raw`).

## Licensing

This is GPLv2 code with the [RMT plugin exception](Copying.RMT) that
permits combining with PokeyForge under the stated terms (non-commercial,
dynamic-linking only).

- **`src/AltirraRMTPOKEY/`, `src/ATAudio/`, `src/ATCore/`** — GPLv2 +
  RMT exception. Per-file headers carry the "alternate license"
  clause referenced by `Copying.RMT`.
- **`src/system/`** — zlib license (a permissive license, per the
  notice at the top of each `system/source/*.cpp`).
- **PokeyForge's modifications** — share the upstream licensing of the
  files they touch (only `src/AltirraRMTPOKEY/source/rmtinterface.cpp`
  is modified; that file is GPLv2 + RMT exception).

PokeyForge consumes the resulting `sa_pokey.dll` via dynamic linking,
which satisfies condition 2 of the RMT exception (run-time linkage with
a separately-distributable library). PokeyForge.exe is distributed free
of charge for non-commercial use, satisfying condition 1.

## Why vendored rather than a submodule

The patch we apply is tiny (~50 lines on top of a ~3 MB source tree)
and the only way to make PokeyForge reproducible from source is to
have those exact bytes under PokeyForge's own version control. A
submodule would point at a moving target on a separate repo we don't
control; a patch file alone would break the moment upstream restructured
the surrounding code. Vendoring trades 3 MB of repo size for a build
that works deterministically on any clone, today and in five years.

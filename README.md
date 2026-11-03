# STDplus

STDplus is an addon build on top of [STD](https://github.com/N8SDR1/STD) by N8SDR.
It adds three features to the base program while tracking upstream STD as a git submodule:

- **TCI dialect detection** — auto-detects Lyra, deskHPSDR, BoltSDR, SDROxide and generic
  ExpertSDR3-compatible servers from the `device:` handshake, and adjusts `START` / `AUDIO_START`
  behaviour accordingly.
- **CW modem** — RX decoder (audio → text via Goertzel / envelope) and TX encoder
  (text → type-2 TCI audio) on the same TCI audio stream STD already uses.
- **N1MM CAT bridge** — TCP server that speaks the N1MM/Ham Radio Deluxe CAT dialect;
  frequency and PTT commands from N1MM macros are forwarded to the TCI radio.

## Building

STDplus compiles the STD sources directly (no fork, no patched files). Only the three
feature files and a thin shim replacing `TciClient` are new.

```
git clone --recurse-submodules https://github.com/pe5jw/STDplus
cd STDplus
cmake -B build
cmake --build build
```

Qt 6.2+, Qt WebSockets, and Qt Network are required — the same dependencies as STD itself.

## Updating STD

```
git submodule update --remote std
git add std
git commit -m "Bump STD to <version>"
```

## Structure

```
std/          upstream STD (submodule, read-only)
src/
  tci/        TciDialect — dialect detection and per-dialect session logic
  cw/         CwModem   — CW RX decoder + TX encoder
  n1mm/       N1mmCat   — N1MM CAT TCP server
CMakeLists.txt
```

## License

GPLv3, the same as STD.

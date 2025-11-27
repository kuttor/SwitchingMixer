# SwitchingMixer Changelog

## Changes Made (2025-11-25)

### Critical Fixes

- **Fixed hardcoded sample rate** (`SwitchingMixer.cpp:380`)
  - Changed `48000.0f` to `NT_globals.sampleRate` for correct timing at all sample rates

### Audio Quality Improvements

- **Per-sample gain smoothing** (`SwitchingMixer.cpp:445-447`)
  - Moved smoothing from per-block to per-sample processing
  - Eliminates zipper noise on fast crossfades
  - Smoothing coefficient now derived from sample rate (~5ms time constant)

- **Per-sample slew processing** (`SwitchingMixer.cpp:437`)
  - Slew rate now calculated and applied per-sample for smoother transitions

- **Improved CV response** (`SwitchingMixer.cpp:409`)
  - Control CV now reads last sample of block (`ctrl[N-1]`) instead of first
  - Better response to fast CV changes

### Bug Fixes

- **MIDI trigger edge detection** (`SwitchingMixer.cpp:494-497`)
  - Added `lastMidiValue` tracking to `MixerGroupState`
  - Trigger modes now only toggle on rising edge (low->high transition)
  - Prevents spam toggles from continuous CC values

- **Enum bounds checking** (`SwitchingMixer.cpp:395-398, 485-486`)
  - Added `std::clamp()` to `ControlType` and `CrossfadeCurve` casts
  - Prevents undefined behavior from out-of-range values

- **Zero slew behavior** (`SwitchingMixer.cpp:414`)
  - Changed condition from `> 0` to `>= 0`
  - Setting per-group slew to 0 now gives instant switching (instead of falling back to global)

### New Features

- **Parameter UI prefixing** (`SwitchingMixer.cpp:522-535`)
  - Implemented `parameterUiPrefix` callback
  - Multi-group configurations now display as `1:Input A L`, `2:Input A L`, etc.
  - Global parameters (Bypass, Global Slew) remain unprefixed

### Code Cleanup

- Removed unused `SMOOTHING_COEFF` constant
- Removed unused `deltaTime` variable

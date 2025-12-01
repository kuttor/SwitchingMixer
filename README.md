# Switching Mixer for Disting NT

A CV/MIDI-controlled and i2c-mappable switching mixer algorithm for the Expert Sleepers Disting NT.

## Overview

```
           ┌─────────────────────┐
Input A ──►│                     │
Input B ──►│   Switching Mixer   ├──► Dest 1 L/R
Control ──►│      (Group N)      ├──► Dest 2 L/R
           │                     ├──► Dest 3 L/R
           │                     ├──► Dest 4 L/R
           └─────────────────────┘
```

## GUID

`SwMx` - NT_MULTICHAR('S', 'w', 'M', 'x')

## Specifications

| Spec   | Range | Default | Description                    |
|--------|-------|---------|--------------------------------|
| Groups | 1-4   | 1       | Number of switch/mix groups    |

## Parameters

### Global (Page 0)

| Parameter    | Range      | Default | Description           |
|--------------|------------|---------|------------------------|
| Bypass       | Off/On     | Off     | Bypass all processing |
| Global Slew  | 0-5000 ms  | 10 ms   | Default slew time     |

### Per Group (Pages 1-4)

| Parameter    | Range       | Default    | Description                    |
|--------------|-------------|------------|--------------------------------|
| Input A L    | Bus 0-28    | Auto       | Input A left/mono bus          |
| Input A R    | Bus 0-28    | 0 (mono)   | Input A right bus (0=mono)     |
| Input B L    | Bus 0-28    | Auto       | Input B left/mono bus          |
| Input B R    | Bus 0-28    | 0 (mono)   | Input B right bus (0=mono)     |
| Control      | Bus 0-28    | Auto       | Control CV input bus           |
| Volume A     | -40 to +6 dB| 0 dB       | Input A volume                 |
| Volume B     | -40 to +6 dB| 0 dB       | Input B volume                 |
| Ctrl Type    | Enum        | Unipolar   | Control response type          |
| Curve        | Enum        | Equal Power| Crossfade curve                |
| Crossfade    | 0-10        | 0          | Per-group slew time            |
| MIDI Enable  | Off/On      | Off        | Enable MIDI control            |
| MIDI Channel | 1-16        | 1          | MIDI channel                   |
| MIDI CC      | 0-127       | Group #    | MIDI CC number                 |
| Dest Count   | 1-4         | 1          | Number of output destinations  |
| Dest 1-4 L/R | Bus 0-28    | Auto/0     | Output destination buses       |

## Control Types

| Type      | Input Range | Position 0 | Position 1 |
|-----------|-------------|------------|------------|
| Unipolar  | 0V to +10V  | 0V         | +10V       |
| Bipolar   | -5V to +5V  | -5V        | +5V        |
| Uni Rev   | 0V to +10V  | +10V       | 0V         |
| Bi Rev    | -5V to +5V  | +5V        | -5V        |
| Trigger   | Rising edge | Toggle A   | Toggle B   |
| Trig Rev  | Rising edge | Toggle B   | Toggle A   |
| Gate      | Gate signal | Low = A    | High = B   |
| Gate Rev  | Gate signal | Low = B    | High = A   |

## Crossfade Curves

| Curve       | Formula                              | Use Case              |
|-------------|--------------------------------------|-----------------------|
| Linear      | A = 1-x, B = x                       | Simple mixing         |
| Equal Power | A = cos(x·π/2), B = sin(x·π/2)       | Constant loudness     |
| S-Curve     | t = x²(3-2x), A = 1-t, B = t         | Smooth DJ-style       |

## Building

This plugin uses the official Disting NT API. To build:

1. Get the distingNT_API from Expert Sleepers
2. Install ARM cross-compilation toolchain
3. Build:

```bash
# Example with ARM GCC
arm-none-eabi-g++ -std=c++17 -O2 -mcpu=cortex-m7 \
    -I/path/to/distingNT_API/include \
    -c SwitchingMixer.cpp -o SwitchingMixer.o

arm-none-eabi-g++ -shared -o SwMx.ntplugin SwitchingMixer.o
```

4. Copy .ntplugin to Disting NT SD card plugins folder

## Usage Examples

### Simple A/B Crossfader
- Set Control Type to "Unipolar"
- Send 0-10V CV to control input
- 0V = Input A, 10V = Input B

### Gate-Controlled Switch
- Set Control Type to "Gate"
- Send gate signal to control
- Low = Input A, High = Input B
- Use Slew for smooth transitions

### MIDI-Controlled Crossfader
- Enable MIDI
- Set channel and CC
- CC 0 = Input A, CC 127 = Input B

## Technical Notes

- Sample rate: 48kHz (assumes standard Disting NT rate)
- Zero latency (no lookahead)
- Output is additive to destination buses
- All signals ±10V compatible

## Author

Me!

## License

MIT License

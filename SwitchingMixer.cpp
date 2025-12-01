/*
 * Switching Mixer (SwMx) *
 * CV/MIDI/I2C-controlled routing mixer for Disting NT.
 * 1-4 groups, each routes one input (mono/stereo) to one of 4 destinations.
 * Controller selects which destination receives the input.
 */

#include <distingnt/api.h>
#include <new>
#include <cmath>
#include <algorithm>

// --- Specification indices ---
enum SpecIndex {
    SPEC_GROUPS = 0,
    SPEC_DESTINATIONS,
    NUM_SPECS
};

// --- Hardware limits ---
constexpr int MAX_GROUPS        = 4;
constexpr int MAX_DESTINATIONS  = 4;
constexpr int MAX_BUSSES        = 28;

// --- Control types ---
enum ControlType {
    CTRL_UNIPOLAR = 0,  // 0-10V selects dest 1-4
    CTRL_BIPOLAR,       // -5V to +5V selects dest 1-4
    CTRL_TRIGGER,       // Rising edge cycles through destinations
    CTRL_TRIG_REV,      // Rising edge cycles backwards
    CTRL_GATE,          // Low=Dest1, High=Dest2
    CTRL_GATE_REV,      // Low=Dest2, High=Dest1
    CTRL_TYPE_COUNT
};

static const char* const controlTypeStrings[] = {
    "Unipolar", "Bipolar", "Trigger", "Trig Rev", "Gate", "Gate Rev", nullptr
};

// --- Crossfade curves (for smooth transitions between destinations) ---
enum CrossfadeCurve {
    CURVE_LINEAR = 0,
    CURVE_EQUAL_POWER,
    CURVE_S_CURVE,
    CURVE_COUNT
};

static const char* const curveStrings[] = {
    "Linear", "Equal Power", "S-Curve", nullptr
};

// Off/On strings for enable parameters
static const char* const offOnStrings[] = {
    "Off", "On", nullptr
};

// --- Constants ---
constexpr float TRIGGER_THRESHOLD = 2.5f;
constexpr float GATE_THRESHOLD    = 2.5f;

// Simple clamp helper (C++11-safe)
template<typename T>
static inline T smxClamp(T v, T lo, T hi) {
    return (v < lo) ? lo : (v > hi ? hi : v);
}

// Sample rate lookup
static float getSampleRateFloat() {
    switch (NT_globals.sampleRate) {
        case 44100:  return 44100.0f;
        case 48000:  return 48000.0f;
        case 88200:  return 88200.0f;
        case 96000:  return 96000.0f;
        default:     return 48000.0f;
    }
}

// --- Parameter indices per group ---
// Note: Actual number of dest params depends on SPEC_DESTINATIONS
enum GroupParamOffset {
    GP_INPUT_L = 0,     // Input left/mono
    GP_INPUT_R,         // Input right (0 = mono, use L for both)
    GP_CONTROL,         // CV control input
    GP_VOLUME,          // Input volume (0..106)
    GP_PAN,             // Pan for the input pair (-50..50)
    GP_CTRL_TYPE,       // Control type
    GP_CURVE,           // Crossfade curve (reserved)
    GP_FADE_TIME,       // Fade amount 0..10 (0=hard switch, 10=slowest)
    GP_DEST_XFADE,      // Dest crossfade: Off/On
    GP_ACTIVE_DEST,     // Active destination (1 to numDests) - mappable!
    GP_DEST1_L,         // Dest params start here
    GP_DEST1_R,
    GP_DEST2_L,
    GP_DEST2_R,
    GP_DEST3_L,
    GP_DEST3_R,
    GP_DEST4_L,
    GP_DEST4_R,
    GP_MIDI_ENABLE,
    GP_MIDI_CHANNEL,
    GP_MIDI_CC,
    PARAMS_PER_GROUP_MAX  // Maximum - actual count depends on numDests
};

// Global params
enum GlobalParam {
    PARAM_BYPASS = 0,
    PARAM_GLOBAL_SLEW,     // Global fade amount 0..10
    GLOBAL_PARAM_COUNT
};

constexpr size_t MAX_PARAMS = GLOBAL_PARAM_COUNT + (MAX_GROUPS * PARAMS_PER_GROUP_MAX);

// --- Per-group runtime state ---
struct MixerGroupState {
    int   currentDest  = 0;  // Current destination index (0-3)
    int   targetDest   = 0;  // Target destination
    float destGains[MAX_DESTINATIONS]   = { 1.0f, 0.0f, 0.0f, 0.0f };  // Gain per destination
    float targetGains[MAX_DESTINATIONS] = { 1.0f, 0.0f, 0.0f, 0.0f };
    bool  lastTriggerHigh = false;
    uint8_t lastMidiValue = 0;
};

/* ───── specifications ───── */
static const _NT_specification gSpecs[] = {
    {
        .name = "Groups",
        .min = 1,
        .max = MAX_GROUPS,
        .def = 1,
        .type = kNT_typeGeneric
    },
    {
        .name = "Destinations",
        .min = 2,
        .max = MAX_DESTINATIONS,
        .def = 2,
        .type = kNT_typeGeneric
    }
};
static_assert(NUM_SPECS == sizeof(gSpecs) / sizeof(gSpecs[0]), "Spec count mismatch");

/* ───── instance ───── */
struct SwitchingMixer : _NT_algorithm {
    uint8_t numGroups;
    uint8_t numDests;
    uint8_t paramsPerGroup;  // Actual params per group (depends on numDests)
    MixerGroupState groupState[MAX_GROUPS];
    _NT_parameter params[MAX_PARAMS];
    
    // Parameter pages
    _NT_parameterPage pageDefs[MAX_GROUPS + 1];  // Global + up to 4 groups
    _NT_parameterPages pagesStruct;
    
    // Parameter indices for each page (sized for max)
    uint8_t globalParamIndices[GLOBAL_PARAM_COUNT];
    uint8_t groupParamIndices[MAX_GROUPS][PARAMS_PER_GROUP_MAX];

    SwitchingMixer() : numGroups(1), numDests(2), paramsPerGroup(0) {}
};

/* ───── helpers ───── */
static inline float* bus(float* b, int bus_idx, int N) {
    return (bus_idx > 0) ? (b + (bus_idx - 1) * N) : nullptr;
}

static inline void setParam(_NT_parameter& p, const char* name,
                            int16_t min, int16_t max, int16_t def, uint8_t unit) {
    p.name   = name;
    p.min    = min;
    p.max    = max;
    p.def    = def;
    p.unit   = unit;
    p.scaling = 0;
    p.enumStrings = nullptr;
}

static inline void setParamEnum(_NT_parameter& p, const char* name,
                                int16_t min, int16_t max, int16_t def,
                                const char* const* strings) {
    p.name   = name;
    p.min    = min;
    p.max    = max;
    p.def    = def;
    p.unit   = kNT_unitEnum;
    p.scaling = 0;
    p.enumStrings = strings;
}

static inline float dbToGain(float db) {
    return std::pow(10.0f, db / 20.0f);
}

/* ───── requirements ───── */
static void calcReq(_NT_algorithmRequirements& r, const int32_t* sp) {
    const int groups = sp[SPEC_GROUPS];
    const int dests  = sp[SPEC_DESTINATIONS];
    
    // Params per group: 10 fixed + (dests * 2 for L/R) + 3 MIDI
    const int paramsPerGroup = 10 + (dests * 2) + 3;
    
    r.numParameters = GLOBAL_PARAM_COUNT + (groups * paramsPerGroup);
    r.sram          = sizeof(SwitchingMixer);
    r.dram          = 0;
    r.dtc           = 0;
    r.itc           = 0;
}

/* ───── constructor ───── */
static _NT_algorithm* construct(const _NT_algorithmMemoryPtrs& m,
                                 const _NT_algorithmRequirements& r,
                                 const int32_t* sp) {
    const uint8_t groups = sp[SPEC_GROUPS];
    const uint8_t dests  = sp[SPEC_DESTINATIONS];
    
    if (groups < 1 || groups > MAX_GROUPS) {
        return nullptr;
    }
    if (dests < 2 || dests > MAX_DESTINATIONS) {
        return nullptr;
    }
    
    SwitchingMixer* self = new (m.sram) SwitchingMixer();
    self->numGroups      = groups;
    self->numDests       = dests;
    // 10 fixed params per group + dest pairs + 3 MIDI
    self->paramsPerGroup = 10 + (dests * 2) + 3;
    
    int p = 0;
    
    // --- Global parameters ---
    setParamEnum(self->params[p++], "Bypass", 0, 1, 0, offOnStrings);
    // 0..10 fade amount (0 = hard switch, 10 = max fade)
    setParam(self->params[p++], "Global Fade", 0, 10, 0, kNT_unitNone);
    
    // Destination name arrays
    static const char* destLNames[] = { "Dest 1 L", "Dest 2 L", "Dest 3 L", "Dest 4 L" };
    static const char* destRNames[] = { "Dest 1 R", "Dest 2 R", "Dest 3 R", "Dest 4 R" };
    
    // --- Per-group parameters ---
    for (int g = 0; g < groups; ++g) {
        // Single input (mono or stereo) – all groups default to In 1/2
        setParam(self->params[p++], "Input L", 0, MAX_BUSSES,
                 1, kNT_unitAudioInput);
        setParam(self->params[p++], "Input R", 0, MAX_BUSSES,
                 2, kNT_unitAudioInput);  // 0 = mono mode; 2 = default stereo R
        
        // Control input (default 0 = none, use Active Dest param)
        setParam(self->params[p++], "Control", 0, MAX_BUSSES,
                 0, kNT_unitCvInput);
        
        // Volume: 0 = off, 100 = 0dB, 106 = +6dB
        setParam(self->params[p++], "Volume", 0, 106, 100, kNT_unitNone);
        
        // Pan: -50..50 (center=0)
        setParam(self->params[p++], "Pan", -50, 50, 0, kNT_unitPercent);
        
        // Control type
        setParamEnum(self->params[p++], "Ctrl Type", 0, CTRL_TYPE_COUNT - 1,
                     CTRL_UNIPOLAR, controlTypeStrings);
        
        // Curve (reserved for future more complex curves)
        setParamEnum(self->params[p++], "Curve", 0, CURVE_COUNT - 1,
                     CURVE_EQUAL_POWER, curveStrings);
        
        // Fade amount (per group) 0..10
        setParam(self->params[p++], "Fade", 0, 10, 0, kNT_unitNone);
        
        // Destination crossfade on/off
        setParamEnum(self->params[p++], "Dest Xfade", 0, 1, 1, offOnStrings);
        
        // Active Destination (1 to numDests) - THIS IS MAPPABLE TO I2C!
        setParam(self->params[p++], "Active Dest", 1, dests, 1, kNT_unitNone);
        
        // Destination pairs (only as many as specified)
        for (int d = 0; d < dests; ++d) {
            // Dest 1..4 -> outputs 1/2, 3/4, 5/6, 7/8
            int outL = 1 + (d * 2); // 1,3,5,7
            int outR = 2 + (d * 2); // 2,4,6,8
            
            setParam(self->params[p++], destLNames[d], 0, MAX_BUSSES, outL, kNT_unitAudioOutput);
            setParam(self->params[p++], destRNames[d], 0, MAX_BUSSES, outR, kNT_unitAudioOutput);
        }
        
        // MIDI
        setParamEnum(self->params[p++], "MIDI Enable", 0, 1, 0, offOnStrings);
        setParam(self->params[p++], "MIDI Channel", 1, 16, 1, kNT_unitNone);
        setParam(self->params[p++], "MIDI CC", 0, 127, g, kNT_unitNone);
    }
    
    // Set the parameters pointer for the base _NT_algorithm struct
    self->parameters = self->params;
    
    // Setup parameter pages
    // Page 0: Global parameters
    for (uint8_t i = 0; i < GLOBAL_PARAM_COUNT; ++i) {
        self->globalParamIndices[i] = i;
    }
    self->pageDefs[0].name      = "Global";
    self->pageDefs[0].numParams = GLOBAL_PARAM_COUNT;
    self->pageDefs[0].params    = self->globalParamIndices;
    
    // Pages 1-N: One per group
    static const char* groupNames[MAX_GROUPS] = { "Group 1", "Group 2", "Group 3", "Group 4" };
    for (int g = 0; g < groups; ++g) {
        const int baseIdx = GLOBAL_PARAM_COUNT + (g * self->paramsPerGroup);
        for (uint8_t i = 0; i < self->paramsPerGroup; ++i) {
            self->groupParamIndices[g][i] = baseIdx + i;
        }
        self->pageDefs[g + 1].name      = groupNames[g];
        self->pageDefs[g + 1].numParams = self->paramsPerGroup;
        self->pageDefs[g + 1].params    = self->groupParamIndices[g];
    }
    
    // Set up the pages structure
    self->pagesStruct.numPages = 1 + groups;  // Global + groups
    self->pagesStruct.pages    = self->pageDefs;
    self->parameterPages       = &self->pagesStruct;
    
    return self;
}

/* ───── control processing ───── */
// Returns target destination index (0 to numDests-1)
static int processControl(float cv, ControlType type, int numDests, MixerGroupState& state) {
    int dest = state.targetDest;
    
    switch (type) {
        case CTRL_UNIPOLAR: {
            // 0V = Dest 1, 10V = Dest N
            float normalized = smxClamp(cv / 10.0f, 0.0f, 0.9999f);
            dest = (int)(normalized * numDests);
            break;
        }
        case CTRL_BIPOLAR: {
            // -5V = Dest 1, +5V = Dest N
            float normalized = smxClamp((cv + 5.0f) / 10.0f, 0.0f, 0.9999f);
            dest = (int)(normalized * numDests);
            break;
        }
        case CTRL_TRIGGER: {
            // Rising edge advances to next destination
            bool high = cv > TRIGGER_THRESHOLD;
            if (high && !state.lastTriggerHigh) {
                dest = (state.targetDest + 1) % numDests;
            }
            state.lastTriggerHigh = high;
            break;
        }
        case CTRL_TRIG_REV: {
            // Rising edge goes to previous destination
            bool high = cv > TRIGGER_THRESHOLD;
            if (high && !state.lastTriggerHigh) {
                dest = (state.targetDest + numDests - 1) % numDests;
            }
            state.lastTriggerHigh = high;
            break;
        }
        case CTRL_GATE:
            // Low = Dest 1, High = Dest 2
            dest = (cv > GATE_THRESHOLD) ? std::min(1, numDests - 1) : 0;
            break;
        case CTRL_GATE_REV:
            // Low = Dest 2, High = Dest 1
            dest = (cv > GATE_THRESHOLD) ? 0 : std::min(1, numDests - 1);
            break;
        default:
            break;
    }
    
    return smxClamp(dest, 0, numDests - 1);
}

/* ───── DSP step ───── */
static void step(_NT_algorithm* b, float* buf, int nBy4) {
    SwitchingMixer* self = static_cast<SwitchingMixer*>(b);
    const int N = nBy4 * 4;
    
    if (self->v[PARAM_BYPASS]) {
        return;
    }
    
    // Global fade amount 0..10
    const float globalFadeAmt = (float)self->v[PARAM_GLOBAL_SLEW];
    const float sampleRate    = getSampleRateFloat();
    const int   numDests      = self->numDests;
    const int   paramsPerGroup = self->paramsPerGroup;
    
    for (int g = 0; g < self->numGroups; ++g) {
        const int base = GLOBAL_PARAM_COUNT + (g * paramsPerGroup);
        MixerGroupState& state = self->groupState[g];
        
        // Get parameters (fixed indices)
        const int inputL    = self->v[base + GP_INPUT_L];
        const int inputR    = self->v[base + GP_INPUT_R];
        const int controlBus = self->v[base + GP_CONTROL];

        // Volume 0..106 (0=off, 100=0dB, 106=+6dB)
        const int volRaw = self->v[base + GP_VOLUME];
        float volume;
        if (volRaw <= 0) {
            volume = 0.0f;
        } else {
            float db = (float)volRaw - 100.0f;
            volume = dbToGain(db);
        }

        // Pan -50..50 -> -1..1
        const int panRaw = self->v[base + GP_PAN];
        float panNorm = smxClamp(panRaw / 50.0f, -1.0f, 1.0f);
        const float angle = (panNorm + 1.0f) * 0.25f * 3.14159265f;
        const float panGL = std::cos(angle);
        const float panGR = std::sin(angle);

        const ControlType ctrlType = (ControlType)smxClamp(
            (int)self->v[base + GP_CTRL_TYPE], 0, (int)CTRL_TYPE_COUNT - 1);
        const CrossfadeCurve curve = (CrossfadeCurve)smxClamp(
            (int)self->v[base + GP_CURVE], 0, (int)CURVE_COUNT - 1);
        (void)curve; // reserved
        
        const float fadeAmtLocal = (float)self->v[base + GP_FADE_TIME]; // 0..10
        const int destXfade = self->v[base + GP_DEST_XFADE] ? 1 : 0;
        
        // Active Dest parameter (1-based, convert to 0-based)
        const int activeDestParam = smxClamp((int)self->v[base + GP_ACTIVE_DEST], 1, numDests) - 1;
        
        // Get input bus pointers
        float* inL  = bus(buf, inputL, N);
        float* inR  = bus(buf, inputR, N);
        float* ctrl = bus(buf, controlBus, N);
        
        // Get destination bus pointers (dynamic based on numDests)
        float* destL[MAX_DESTINATIONS];
        float* destR[MAX_DESTINATIONS];
        for (int d = 0; d < numDests; ++d) {
            destL[d] = bus(buf, self->v[base + GP_DEST1_L + d * 2], N);
            destR[d] = bus(buf, self->v[base + GP_DEST1_R + d * 2], N);
        }
        
        // Determine target destination
        if (ctrl) {
            state.targetDest = processControl(ctrl[N - 1], ctrlType, numDests, state);
        } else {
            state.targetDest = activeDestParam;
        }
        
        // Update target gains
        for (int d = 0; d < numDests; ++d) {
            state.targetGains[d] = (d == state.targetDest) ? 1.0f : 0.0f;
        }

        // Effective fade amount: per-group overrides global if >0
        float fadeAmt = (fadeAmtLocal > 0.0f) ? fadeAmtLocal : globalFadeAmt;

        float slewRate;
        if (!destXfade || fadeAmt <= 0.0f) {
            // 0 = off -> hard switch
            slewRate = 1.0f;
        } else {
            // Map 1..10 to ~0.1s..5s fade times
            const float maxFadeSec  = 5.0f;
            const float fadeTimeSec = (fadeAmt / 10.0f) * maxFadeSec;
            slewRate = 1.0f - std::exp(-1.0f / (sampleRate * fadeTimeSec));
        }

        // Process audio
        for (int n = 0; n < N; ++n) {
            // Get raw input samples
            float inSampleL = inL ? inL[n] : 0.0f;
            float inSampleR = inR ? inR[n] : inSampleL;  // if no R, duplicate L

            // Treat the input pair as a single mono source
            float mono = 0.5f * (inSampleL + inSampleR);

            // Apply volume
            mono *= volume;

            // Apply pan to create L/R
            float sigL = mono * panGL;
            float sigR = mono * panGR;
            
            // Slew the destination gains (or snap if slewRate == 1)
            for (int d = 0; d < numDests; ++d) {
                state.destGains[d] += (state.targetGains[d] - state.destGains[d]) * slewRate;
            }
            
            // Output to each destination based on its gain
            for (int d = 0; d < numDests; ++d) {
                float gain = state.destGains[d];
                if (gain > 0.0001f) {  // Only write if gain is significant
                    if (destL[d]) destL[d][n] += sigL * gain;
                    if (destR[d]) destR[d][n] += sigR * gain;
                }
            }
        }
    }
}

/* ───── MIDI handling ───── */
static void midiMessage(_NT_algorithm* b, uint8_t byte0, uint8_t byte1, uint8_t byte2) {
    SwitchingMixer* self = static_cast<SwitchingMixer*>(b);

    const uint8_t status  = byte0 & 0xF0;
    const uint8_t channel = (byte0 & 0x0F) + 1;

    if (status != 0xB0) return; // CC only

    const int numDests      = self->numDests;
    const int paramsPerGroup = self->paramsPerGroup;
    
    // MIDI params are at end of each group: after dest params
    // GP_DEST1_L + (numDests * 2) = first MIDI param
    const int midiEnableOffset  = GP_DEST1_L + (numDests * 2);
    const int midiChannelOffset = midiEnableOffset + 1;
    const int midiCCOffset      = midiEnableOffset + 2;

    for (int g = 0; g < self->numGroups; ++g) {
        const int base = GLOBAL_PARAM_COUNT + (g * paramsPerGroup);

        if (!self->v[base + midiEnableOffset]) continue;
        if (channel != self->v[base + midiChannelOffset]) continue;
        if (byte1  != self->v[base + midiCCOffset])      continue;

        MixerGroupState& state = self->groupState[g];
        const ControlType ctrlType = (ControlType)smxClamp(
            (int)self->v[base + GP_CTRL_TYPE], 0, (int)CTRL_TYPE_COUNT - 1);
        
        int dest;
        
        switch (ctrlType) {
            case CTRL_UNIPOLAR:
            case CTRL_BIPOLAR: {
                // CC 0-127 maps to destinations
                float normalized = (byte2 / 127.0f) * 0.9999f;
                dest = (int)(normalized * numDests);
                break;
            }
            case CTRL_TRIGGER: {
                bool wasHigh = state.lastMidiValue > 63;
                bool isHigh  = byte2 > 63;
                if (isHigh && !wasHigh) {
                    dest = (state.targetDest + 1) % numDests;
                } else {
                    dest = state.targetDest;
                }
                break;
            }
            case CTRL_TRIG_REV: {
                bool wasHigh = state.lastMidiValue > 63;
                bool isHigh  = byte2 > 63;
                if (isHigh && !wasHigh) {
                    dest = (state.targetDest + numDests - 1) % numDests;
                } else {
                    dest = state.targetDest;
                }
                break;
            }
            case CTRL_GATE:
                dest = (byte2 > 63) ? std::min(1, numDests - 1) : 0;
                break;
            case CTRL_GATE_REV:
                dest = (byte2 > 63) ? 0 : std::min(1, numDests - 1);
                break;
            default:
                dest = state.targetDest;
                break;
        }
        
        state.lastMidiValue = byte2;
        state.targetDest    = smxClamp(dest, 0, numDests - 1);
        
        // Update target gains
        for (int d = 0; d < numDests; ++d) {
            state.targetGains[d] = (d == state.targetDest) ? 1.0f : 0.0f;
        }
    }
}

/* ───── parameter UI prefix ───── */
static int parameterUiPrefix(_NT_algorithm* alg, int p, char* buff) {
    SwitchingMixer* self = static_cast<SwitchingMixer*>(alg);
    if (p < GLOBAL_PARAM_COUNT) {
        return 0;
    }
    int groupIndex = (p - GLOBAL_PARAM_COUNT) / self->paramsPerGroup;
    int len = NT_intToString(buff, 1 + groupIndex);
    buff[len++] = ':';
    buff[len]   = '\0';
    return len;
}

/* ───── factory & entry ───── */
static const _NT_factory gFactory = {
    .guid                 = NT_MULTICHAR('S', 'w', 'M', 'x'),
    .name                 = "Switching Mixer",
    .description          = "Routes input to one of multiple destinations via CV/MIDI. "
                            "Optional crossfade with per-group fade.",
    .numSpecifications    = sizeof(gSpecs) / sizeof(gSpecs[0]),
    .specifications       = gSpecs,
    .calculateStaticRequirements = nullptr,
    .initialise           = nullptr,
    .calculateRequirements = calcReq,
    .construct            = construct,
    .parameterChanged     = nullptr,
    .step                 = step,
    .draw                 = nullptr,
    .midiRealtime         = nullptr,
    .midiMessage          = midiMessage,
    .tags                 = kNT_tagUtility,
    .hasCustomUi          = nullptr,
    .customUi             = nullptr,
    .setupUi              = nullptr,
    .parameterUiPrefix    = parameterUiPrefix
};

extern "C" uintptr_t pluginEntry(_NT_selector s, uint32_t i) {
    switch (s) {
        case kNT_selector_version:
            return kNT_apiVersionCurrent;
        case kNT_selector_numFactories:
            return 1;
        case kNT_selector_factoryInfo:
            return (i == 0) ? reinterpret_cast<uintptr_t>(&gFactory) : 0;
        default:
            return 0;
    }
}

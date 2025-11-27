⏺ Switching Mixer Plugin - Code Review

  Hey! Nice plugin - the architecture is clean and the feature set is solid. Found a few issues
   you'll want to address, roughly in priority order.

  ---
  Critical: Hardcoded Sample Rate

  Location: SwitchingMixer.cpp:380

  const float sampleRate = 48000.0f; // TODO: Get from API if available

  The distingNT supports multiple sample rates, and the API provides the actual rate. This
  breaks all your timing calculations (slew, smoothing) at any sample rate other than 48kHz.

  Fix:
  const float sampleRate = (float)NT_globals.sampleRate;

  You'll need to include or reference the globals - they're declared in the API header as
  extern const _NT_globals NT_globals;

  ---
  Moderate: Smoothing Happens Per-Block Instead of Per-Sample

  Location: SwitchingMixer.cpp:428-429

  state.smoothedGainA = state.smoothedGainA * SMOOTHING_COEFF + gainA * (1.0f -
  SMOOTHING_COEFF);
  state.smoothedGainB = state.smoothedGainB * SMOOTHING_COEFF + gainB * (1.0f -
  SMOOTHING_COEFF);

  This runs once per block (~128 samples), not per sample. You'll get zipper noise on fast
  crossfades.

  Fix: Move the smoothing inside the sample loop:

  // Before the sample loop, calculate per-sample smoothing coefficient
  float smoothCoeff = 0.999f;  // Tune to taste, or derive from sample rate

  // Inside the loop
  for (int n = 0; n < N; ++n) {
      state.smoothedGainA = state.smoothedGainA * smoothCoeff + gainA * (1.0f - smoothCoeff);
      state.smoothedGainB = state.smoothedGainB * smoothCoeff + gainB * (1.0f - smoothCoeff);

      // ... rest of sample processing using smoothedGainA/B
  }

  ---
  Moderate: Control CV Only Reads First Sample

  Location: SwitchingMixer.cpp:409

  state.targetPosition = processControl(ctrl[0], ctrlType, state);

  Only reads sample 0 of the control input. Fast CV or audio-rate modulation will stair-step.

  Fix: Either process CV per-sample (more CPU), or at minimum read multiple times per block:

  // Simple fix - read at block boundaries
  if (ctrl) {
      state.targetPosition = processControl(ctrl[N-1], ctrlType, state);
  }

  Or for audio-rate response, move processControl() inside the sample loop.

  ---
  Minor: MIDI Trigger Mode Can Spam Toggles

  Location: SwitchingMixer.cpp:483-489

  case CTRL_TRIGGER:
  case CTRL_TRIG_REV:
      if (msg->byte2 > 63) {
          state.triggerState = !state.triggerState;

  If someone sends continuous CC values above 63, this toggles every message. Should detect
  rising edge only.

  Fix: Track previous MIDI value:

  // Add to MixerGroupState
  uint8_t lastMidiValue = 0;

  // In midiMessage()
  case CTRL_TRIGGER:
  case CTRL_TRIG_REV: {
      bool wasHigh = state.lastMidiValue > 63;
      bool isHigh = msg->byte2 > 63;
      if (isHigh && !wasHigh) {
          state.triggerState = !state.triggerState;
      }
      state.lastMidiValue = msg->byte2;
      position = (ctrlType == CTRL_TRIGGER)
          ? (state.triggerState ? 1.0f : 0.0f)
          : (state.triggerState ? 0.0f : 1.0f);
      break;
  }

  ---
  Minor: No Bounds Check on Enum Casts

  Location: SwitchingMixer.cpp:395-396

  const ControlType ctrlType = (ControlType)self->v[base + GP_CTRL_TYPE];
  const CrossfadeCurve curve = (CrossfadeCurve)self->v[base + GP_CURVE];

  If values are somehow out of range, undefined behavior. Probably fine in practice, but easy
  to guard:

  const ControlType ctrlType = (ControlType)std::clamp(
      self->v[base + GP_CTRL_TYPE], 0, (int)CTRL_TYPE_COUNT - 1);
  const CrossfadeCurve curve = (CrossfadeCurve)std::clamp(
      self->v[base + GP_CURVE], 0, (int)CURVE_COUNT - 1);

  ---
  Minor: Zero Slew Falls Back to Global (Maybe Unintentional?)

  Location: SwitchingMixer.cpp:413

  float slewTime = (slewMs > 0.0f) ? slewMs : globalSlewMs;

  If someone sets per-group slew to 0, expecting instant switching, they get the global slew
  instead. If this is intentional, maybe document it. If not:

  // Use -1 or a checkbox to mean "use global"
  float slewTime = (slewMs >= 0.0f) ? slewMs : globalSlewMs;

  ---
  Nitpick: CMakeLists.txt Output Type

  The CMake produces a .ntplugin shared library, but distingNT wants relocatable .o object
  files. Your README mentions the right approach with arm-none-eabi-g++ -c, but the CMake
  doesn't match. Might confuse someone trying to build with CMake.

  ---
  What's Good

  - Clean struct layout and parameter organization
  - Parameter pages are set up correctly
  - Null checks on bus pointers
  - Trigger/gate detection logic for CV is solid
  - Good variety of control modes and curves
  - MIDI implementation is mostly correct

  Overall it's a well-written plugin. The sample rate issue is the only thing I'd call a
  must-fix before shipping. The smoothing stuff will affect audio quality but won't crash
  anything.
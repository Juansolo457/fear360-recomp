// F.E.A.R. PC - one game controller, read through SDL3.
//
// Every connected pad drives the player (the 360 game's one-player model): the state is the
// latest-used pad's. Sticks and triggers come back with their dead zones applied, so a value of 0
// means at rest. Nothing here knows about the engine; pc/game/src/fear_pad.cpp feeds it in.
#pragma once

#include <cstdint>

namespace fearpc {

enum class PadInput : uint8_t {
  kLeftX,  // -1 left .. 1 right
  kLeftY,  // -1 down .. 1 up
  kRightX,
  kRightY,
  kLeftTrigger,  // 0 .. 1
  kRightTrigger,
  kA,  // buttons: 0 or 1, by position (A is the bottom face button on every pad)
  kB,
  kX,
  kY,
  kBack,
  kStart,
  kLeftStick,
  kRightStick,
  kLeftShoulder,
  kRightShoulder,
  kDpadUp,
  kDpadDown,
  kDpadLeft,
  kDpadRight,
  kCount,
};

// What the pad looks like, for button prompts.
enum class PadFamily : uint8_t { kNone, kXbox, kPlayStation, kNintendo, kGeneric };

struct PadState {
  bool connected = false;
  PadFamily family = PadFamily::kNone;
  float value[static_cast<int>(PadInput::kCount)] = {};

  float operator[](PadInput input) const { return value[static_cast<int>(input)]; }
  bool Down(PadInput input) const { return value[static_cast<int>(input)] >= 0.5f; }
};

// Dead zones, as fractions of full travel. The defaults are XInput's recommended ones, which
// the 360 itself used.
struct PadDeadZones {
  float left_stick = 7849.0f / 32767.0f;
  float right_stick = 8689.0f / 32767.0f;
  float trigger = 30.0f / 255.0f;
};

// Radial dead zone: inside it 0, outside rescaled so the edge of the zone reads 0 and full
// travel reads 1, keeping the direction. Used for both sticks.
void ApplyStickDeadZone(float dead_zone, float* x, float* y);
// Same along one axis, for triggers.
float ApplyTriggerDeadZone(float dead_zone, float value);

// Starts SDL's gamepad subsystem. Safe to call again; false if SDL could not start.
bool PadInit();
void PadShutdown();
// Polls the pads. Call once per frame.
const PadState& PadUpdate(const PadDeadZones& dead_zones = PadDeadZones());
const PadState& PadCurrent();
// Rumble on the active pad, motor strengths 0..1.
void PadRumble(float low, float high, uint32_t duration_ms);

}  // namespace fearpc

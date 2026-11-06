// F.E.A.R. PC - game controllers through SDL3. See fearpc/pad.h.
#include "fearpc/pad.h"

#include <SDL3/SDL.h>

#include <cmath>

namespace fearpc {

namespace {

bool g_started = false;
PadState g_state;
SDL_JoystickID g_active = 0;  // the pad used last

constexpr struct {
  SDL_GamepadButton button;
  PadInput input;
} kButtons[] = {
    {SDL_GAMEPAD_BUTTON_SOUTH, PadInput::kA},
    {SDL_GAMEPAD_BUTTON_EAST, PadInput::kB},
    {SDL_GAMEPAD_BUTTON_WEST, PadInput::kX},
    {SDL_GAMEPAD_BUTTON_NORTH, PadInput::kY},
    {SDL_GAMEPAD_BUTTON_BACK, PadInput::kBack},
    {SDL_GAMEPAD_BUTTON_START, PadInput::kStart},
    {SDL_GAMEPAD_BUTTON_LEFT_STICK, PadInput::kLeftStick},
    {SDL_GAMEPAD_BUTTON_RIGHT_STICK, PadInput::kRightStick},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, PadInput::kLeftShoulder},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PadInput::kRightShoulder},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, PadInput::kDpadUp},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PadInput::kDpadDown},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PadInput::kDpadLeft},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PadInput::kDpadRight},
};

float Axis(SDL_Gamepad* pad, SDL_GamepadAxis axis) {
  const float v = SDL_GetGamepadAxis(pad, axis) / 32767.0f;
  return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
}

PadFamily Family(SDL_Gamepad* pad) {
  switch (SDL_GetGamepadType(pad)) {
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE:
      return PadFamily::kXbox;
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5:
      return PadFamily::kPlayStation;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
      return PadFamily::kNintendo;
    default:
      return PadFamily::kGeneric;
  }
}

bool AnyInput(const PadState& s) {
  for (float v : s.value) {
    if (std::fabs(v) > 0.0f) return true;
  }
  return false;
}

void Read(SDL_Gamepad* pad, const PadDeadZones& dz, PadState* s) {
  s->connected = true;
  s->family = Family(pad);
  float lx = Axis(pad, SDL_GAMEPAD_AXIS_LEFTX), ly = -Axis(pad, SDL_GAMEPAD_AXIS_LEFTY);
  float rx = Axis(pad, SDL_GAMEPAD_AXIS_RIGHTX), ry = -Axis(pad, SDL_GAMEPAD_AXIS_RIGHTY);
  ApplyStickDeadZone(dz.left_stick, &lx, &ly);
  ApplyStickDeadZone(dz.right_stick, &rx, &ry);
  s->value[static_cast<int>(PadInput::kLeftX)] = lx;
  s->value[static_cast<int>(PadInput::kLeftY)] = ly;
  s->value[static_cast<int>(PadInput::kRightX)] = rx;
  s->value[static_cast<int>(PadInput::kRightY)] = ry;
  // SDL3 reports triggers 0 .. 32767.
  s->value[static_cast<int>(PadInput::kLeftTrigger)] =
      ApplyTriggerDeadZone(dz.trigger, Axis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
  s->value[static_cast<int>(PadInput::kRightTrigger)] =
      ApplyTriggerDeadZone(dz.trigger, Axis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
  for (const auto& b : kButtons) {
    s->value[static_cast<int>(b.input)] = SDL_GetGamepadButton(pad, b.button) ? 1.0f : 0.0f;
  }
}

}  // namespace

bool PadInit() {
  if (g_started) return true;
  // The game's window isn't SDL's, so SDL never sees itself focused: read pads regardless and
  // let the caller decide when input counts. A thread keeps raw input flowing without a window.
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");
  if (!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC)) return false;
  g_started = true;
  return true;
}

void PadShutdown() {
  if (!g_started) return;
  SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC);
  g_started = false;
  g_state = PadState();
}

const PadState& PadUpdate(const PadDeadZones& dead_zones) {
  g_state = PadState();
  if (!g_started) return g_state;
  SDL_UpdateGamepads();

  // The state is the pad used last: a pad with any input takes over; with none in use, the last
  // one used, else the first.
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  PadState first, last_used, in_use;
  for (int i = 0; ids && i < count; ++i) {
    SDL_Gamepad* pad = SDL_GetGamepadFromID(ids[i]);
    if (!pad) pad = SDL_OpenGamepad(ids[i]);
    if (!pad) continue;
    PadState s;
    Read(pad, dead_zones, &s);
    if (!first.connected) first = s;
    if (ids[i] == g_active) last_used = s;
    if (!in_use.connected && AnyInput(s)) {
      in_use = s;
      g_active = ids[i];
    }
  }
  SDL_free(ids);
  g_state = in_use.connected ? in_use : (last_used.connected ? last_used : first);
  return g_state;
}

const PadState& PadCurrent() { return g_state; }

void PadRumble(float low, float high, uint32_t duration_ms) {
  if (!g_started || !g_active) return;
  SDL_Gamepad* pad = SDL_GetGamepadFromID(g_active);
  if (!pad) return;
  auto motor = [](float v) {
    return static_cast<Uint16>((v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v)) * 65535.0f);
  };
  SDL_RumbleGamepad(pad, motor(low), motor(high), duration_ms);
}

}  // namespace fearpc

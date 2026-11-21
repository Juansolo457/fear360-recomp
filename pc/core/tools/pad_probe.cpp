// F.E.A.R. PC - prints the pads SDL sees and their state, through the same code the game uses.
//   fearpc_pad_probe [seconds]           (default 10)
//   fearpc_pad_probe --rumble [seconds]  sends rumble 60 times a second and times each call
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "fearpc/pad.h"

namespace {

const char* FamilyName(fearpc::PadFamily f) {
  switch (f) {
    case fearpc::PadFamily::kXbox: return "Xbox";
    case fearpc::PadFamily::kPlayStation: return "PlayStation";
    case fearpc::PadFamily::kNintendo: return "Nintendo";
    case fearpc::PadFamily::kGeneric: return "generic";
    default: return "none";
  }
}

}  // namespace

// How long each rumble call takes, at the rate the game would send them.
int RumbleTiming(int seconds) {
  fearpc::PadUpdate();
  double worst = 0.0, total = 0.0;
  int calls = 0, slow = 0;
  for (int i = 0; i < seconds * 60; ++i) {
    fearpc::PadUpdate();
    const float strength = (i / 30) % 2 ? 0.6f : 0.2f;
    const Uint64 start = SDL_GetPerformanceCounter();
    fearpc::PadRumble(strength, strength, 150);
    const double ms = (SDL_GetPerformanceCounter() - start) * 1000.0 / SDL_GetPerformanceFrequency();
    worst = ms > worst ? ms : worst;
    total += ms;
    slow += ms > 4.0;
    ++calls;
    SDL_Delay(16);
  }
  fearpc::PadRumble(0.0f, 0.0f, 0);
  std::printf("%d rumble calls: average %.3f ms, worst %.3f ms, %d over 4 ms\n", calls, total / calls, worst, slow);
  return 0;
}

int main(int argc, char** argv) {
  const bool rumble = argc > 1 && std::strcmp(argv[1], "--rumble") == 0;
  const int seconds = argc > (rumble ? 2 : 1) ? std::atoi(argv[rumble ? 2 : 1]) : (rumble ? 5 : 10);
  if (!fearpc::PadInit()) {
    std::printf("SDL could not start: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Delay(500);  // let device discovery finish
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  std::printf("%d pad(s)\n", count);
  for (int i = 0; ids && i < count; ++i)
    std::printf("  %s\n", SDL_GetGamepadNameForID(ids[i]) ? SDL_GetGamepadNameForID(ids[i]) : "?");
  SDL_free(ids);
  if (rumble) {
    const int result = RumbleTiming(seconds);
    fearpc::PadShutdown();
    return result;
  }

  using fearpc::PadInput;
  for (int t = 0; t < seconds * 10; ++t) {
    const fearpc::PadState& s = fearpc::PadUpdate();
    std::printf("\r%-11s L %+.2f %+.2f  R %+.2f %+.2f  LT %.2f RT %.2f  A%d B%d X%d Y%d LB%d RB%d   ",
                s.connected ? FamilyName(s.family) : "no pad", s[PadInput::kLeftX], s[PadInput::kLeftY],
                s[PadInput::kRightX], s[PadInput::kRightY], s[PadInput::kLeftTrigger],
                s[PadInput::kRightTrigger], s.Down(PadInput::kA), s.Down(PadInput::kB), s.Down(PadInput::kX),
                s.Down(PadInput::kY), s.Down(PadInput::kLeftShoulder), s.Down(PadInput::kRightShoulder));
    std::fflush(stdout);
    SDL_Delay(100);
  }
  std::printf("\n");
  fearpc::PadShutdown();
  return 0;
}

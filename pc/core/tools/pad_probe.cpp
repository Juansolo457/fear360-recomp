// F.E.A.R. PC - prints the pads SDL sees and their state, through the same code the game uses.
//   fearpc_pad_probe [seconds]   (default 10)
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>

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

int main(int argc, char** argv) {
  const int seconds = argc > 1 ? std::atoi(argv[1]) : 10;
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

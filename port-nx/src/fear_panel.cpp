// F.E.A.R. 360 - the look and the input of the port's own panels. See fear_panel.h.

#include "fear_panel.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>

#include <imgui.h>
#include <rex/logging.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xinput.h>
#else
#include <mutex>
#include <vector>

#include <SDL3/SDL.h>
#endif

namespace fear::panel {
namespace {

ImFont* g_font = nullptr;
std::atomic<int> g_open{0};               // panels open
std::atomic<bool> g_wait_release{false};  // one closed: the game waits for the buttons to be let go

#if defined(_WIN32)
using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

XInputGetStateFn XInputGet() {
  static const XInputGetStateFn function = [] {
    HMODULE module = LoadLibraryW(L"xinput1_4.dll");
    if (!module) module = LoadLibraryW(L"xinput9_1_0.dll");
    return module ? reinterpret_cast<XInputGetStateFn>(GetProcAddress(module, "XInputGetState"))
                  : nullptr;
  }();
  return function;
}
#else
// XInput's button bits, for the SDL path below.
constexpr uint16_t XINPUT_GAMEPAD_DPAD_UP = 0x0001;
constexpr uint16_t XINPUT_GAMEPAD_DPAD_DOWN = 0x0002;
constexpr uint16_t XINPUT_GAMEPAD_DPAD_LEFT = 0x0004;
constexpr uint16_t XINPUT_GAMEPAD_DPAD_RIGHT = 0x0008;
constexpr uint16_t XINPUT_GAMEPAD_A = 0x1000;
constexpr uint16_t XINPUT_GAMEPAD_B = 0x2000;

// Linux: SDL3's gamepads, opened by the panels themselves. The SDK's SDL driver can't stand in:
// the game's active callback (fear_app.h) turns it off while a panel is open, and the collection
// menu opens before the runtime (and its driver) exists. SDL counts subsystem inits, so sharing
// the gamepad subsystem with the driver is fine. Opening and updating happen on the UI thread
// (Navigator::Poll); the input thread (GameGetsInput) only reads the opened pads, which SDL locks.
std::mutex g_sdl_mutex;
std::vector<SDL_Gamepad*> g_sdl_pads;
bool g_sdl_initialized = false;  // UI thread

void UpdateSdlPads() {
  static bool failed = false;
  if (failed) return;
  if (!g_sdl_initialized) {
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
      REXLOG_WARN("F.E.A.R. panels: SDL gamepads unavailable: {}", SDL_GetError());
      failed = true;
      return;
    }
    g_sdl_initialized = true;
  }
  SDL_UpdateGamepads();
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  std::lock_guard<std::mutex> lock(g_sdl_mutex);
  // Pads that went away are closed; new ones opened (SDL hands back the same one when it's open).
  std::erase_if(g_sdl_pads, [](SDL_Gamepad* pad) {
    if (SDL_GamepadConnected(pad)) return false;
    SDL_CloseGamepad(pad);
    return true;
  });
  for (int i = 0; ids && i < count; ++i) {
    SDL_Gamepad* pad = SDL_GetGamepadFromID(ids[i]);
    if (pad && std::find(g_sdl_pads.begin(), g_sdl_pads.end(), pad) != g_sdl_pads.end()) continue;
    if ((pad = SDL_OpenGamepad(ids[i]))) {
      g_sdl_pads.push_back(pad);
      REXLOG_INFO("F.E.A.R. panels: controller '{}' ({:04x}:{:04x})", SDL_GetGamepadName(pad),
                  SDL_GetGamepadVendor(pad), SDL_GetGamepadProduct(pad));
    }
  }
  SDL_free(ids);
}
#endif

struct PadState {
  uint16_t buttons = 0;  // XINPUT_GAMEPAD_*
  int16_t lx = 0, ly = 0;
  bool triggers = false;
};

// Every connected controller merged (all of them drive player 1). Asking XInput about an empty
// slot is slow, so those are asked again once a second.
PadState ReadPads() {
  PadState out;
#if !defined(_WIN32)
  std::lock_guard<std::mutex> lock(g_sdl_mutex);
  for (SDL_Gamepad* pad : g_sdl_pads) {
    const auto button = [pad](SDL_GamepadButton b) { return SDL_GetGamepadButton(pad, b); };
    if (button(SDL_GAMEPAD_BUTTON_DPAD_UP)) out.buttons |= XINPUT_GAMEPAD_DPAD_UP;
    if (button(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) out.buttons |= XINPUT_GAMEPAD_DPAD_DOWN;
    if (button(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) out.buttons |= XINPUT_GAMEPAD_DPAD_LEFT;
    if (button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) out.buttons |= XINPUT_GAMEPAD_DPAD_RIGHT;
    if (button(SDL_GAMEPAD_BUTTON_SOUTH)) out.buttons |= XINPUT_GAMEPAD_A;
    if (button(SDL_GAMEPAD_BUTTON_EAST)) out.buttons |= XINPUT_GAMEPAD_B;
    // SDL's Y axis points down, XInput's up.
    const int16_t lx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
    const int16_t ly = int16_t(-std::max<int>(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY), -32767));
    if (std::abs(lx) > std::abs(out.lx)) out.lx = lx;
    if (std::abs(ly) > std::abs(out.ly)) out.ly = ly;
    // SDL's triggers run 0 to 32767; XInput's threshold of 30 of 255.
    out.triggers |= SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 3855 ||
                    SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 3855;
  }
#else
  static std::atomic<uint32_t> connected{0xF};
  static std::atomic<int64_t> next_scan{0};
  const auto get = XInputGet();
  if (!get) return out;
  const int64_t now = std::chrono::steady_clock::now().time_since_epoch().count();
  const bool scan = now >= next_scan.load();
  if (scan) {
    next_scan = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                          std::chrono::seconds(1)).count();
  }
  for (DWORD i = 0; i < 4; ++i) {
    if (!(connected.load() & (1u << i)) && !scan) continue;
    XINPUT_STATE state{};
    if (get(i, &state) != ERROR_SUCCESS) {
      connected &= ~(1u << i);
      continue;
    }
    connected |= 1u << i;
    const XINPUT_GAMEPAD& pad = state.Gamepad;
    out.buttons |= pad.wButtons;
    if (std::abs(pad.sThumbLX) > std::abs(out.lx)) out.lx = pad.sThumbLX;
    if (std::abs(pad.sThumbLY) > std::abs(out.ly)) out.ly = pad.sThumbLY;
    out.triggers |= pad.bLeftTrigger > 30 || pad.bRightTrigger > 30;
  }
#endif
  return out;
}

// The keys the panels answer to; the game maps some of them to buttons.
bool PanelKeysHeld() {
#if defined(_WIN32)
  for (int key : {VK_RETURN, VK_SPACE, VK_ESCAPE, VK_BACK, VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT}) {
    if (GetAsyncKeyState(key) & 0x8000) return true;
  }
#endif
  // Linux: the keys reach the game through the window, and the panels through ImGui; with a panel
  // just closed there is no ImGui frame to ask from this thread. The controller is the Deck's input.
  return false;
}

}  // namespace

uint32_t Navigator::Poll() {
  const double t = ImGui::GetTime();
  if (opened_ < 0) opened_ = t;
#if !defined(_WIN32)
  UpdateSdlPads();
#endif
  const PadState pad = ReadPads();
  uint32_t now = 0;
  if ((pad.buttons & XINPUT_GAMEPAD_DPAD_UP) || pad.ly > 16000) now |= kUp;
  if ((pad.buttons & XINPUT_GAMEPAD_DPAD_DOWN) || pad.ly < -16000) now |= kDown;
  if ((pad.buttons & XINPUT_GAMEPAD_DPAD_LEFT) || pad.lx < -16000) now |= kLeft;
  if ((pad.buttons & XINPUT_GAMEPAD_DPAD_RIGHT) || pad.lx > 16000) now |= kRight;
  if (pad.buttons & XINPUT_GAMEPAD_A) now |= kAccept;
  if (pad.buttons & XINPUT_GAMEPAD_B) now |= kBack;
  uint32_t pressed = now & ~held_;
  const uint32_t directions = now & kDirections;
  if (pressed & kDirections) {
    next_repeat_ = t + 0.4;
  } else if (directions && (held_ & directions) == directions && t >= next_repeat_) {
    pressed |= directions;
    next_repeat_ = t + 0.11;
  }
  held_ = now;

  // ImGui gets the key that opened the panel too, queued from before it existed.
  if (t - opened_ > 0.25) {
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) pressed |= kUp;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) pressed |= kDown;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) pressed |= kLeft;
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) pressed |= kRight;
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
      pressed |= kAccept;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
      pressed |= kBack;
    }
  }
  return pressed;
}

Panel::Panel(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) { ++g_open; }

Panel::~Panel() {
  g_wait_release = true;
  --g_open;
}

bool AnyOpen() { return g_open.load() > 0; }

float FontSize() {
  return std::max(18.0f, 64.0f * ImGui::GetIO().DisplaySize.y / 2160.0f);
}

void PushFont(float scale) { ImGui::PushFont(g_font, FontSize() * scale); }

void PopFont() { ImGui::PopFont(); }

bool BeginPanel(const char* id, float width_em) {
  const ImGuiIO& io = ImGui::GetIO();
  const float size = FontSize();
  PushFont();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(size * 1.0f, size * 0.8f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(size * 0.4f, size * 0.15f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.04f, 0.05f, 0.94f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.55f, 0.55f, 0.58f, 0.6f));
  ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.62f, 0.10f, 0.08f, 0.85f));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.62f, 0.10f, 0.08f, 0.85f));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.75f, 0.14f, 0.10f, 0.95f));
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                          ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(std::min(size * width_em, io.DisplaySize.x * 0.95f), 0.0f),
                           ImGuiCond_Always);
  ImGui::SetNextWindowFocus();
  return ImGui::Begin(id, nullptr,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
}

void EndPanel() {
  ImGui::End();
  ImGui::PopStyleColor(5);
  ImGui::PopStyleVar(4);
  PopFont();
}

void Dim(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.62f, 0.64f, 1.0f));
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

bool Row(const char* label, int index, int& selected) {
  ImGui::PushID(index);
  const bool clicked = ImGui::Selectable(label, selected == index,
                                         ImGuiSelectableFlags_SpanAllColumns |
                                             ImGuiSelectableFlags_AllowOverlap);
  const ImGuiIO& io = ImGui::GetIO();
  if (ImGui::IsItemHovered() && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
    selected = index;
  }
  if (clicked) selected = index;
  ImGui::PopID();
  return clicked;
}

void AddFonts(ImFontAtlas* atlas) {
  static const ImWchar ranges[] = {0x0020, 0x00FF, 0};
  ImFontConfig config;
  config.OversampleH = config.OversampleV = 2;
  // Linux: SteamOS's Noto Sans (Arch's path), then the Debian/Ubuntu paths.
  for (const char* path : {"C:\\Windows\\Fonts\\bahnschrift.ttf",
                           "C:\\Windows\\Fonts\\segoeui.ttf",
                           "/usr/share/fonts/noto/NotoSans-Regular.ttf",
                           "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
                           "/usr/share/fonts/TTF/DejaVuSans.ttf",
                           "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"}) {
    std::error_code error;
    if (std::filesystem::exists(path, error)) {
      g_font = atlas->AddFontFromFileTTF(path, 64.0f, &config, ranges);
      if (g_font) return;
    }
  }
}

void ReleaseControllers() {
#if !defined(_WIN32)
  if (!g_sdl_initialized) return;
  {
    std::lock_guard<std::mutex> lock(g_sdl_mutex);
    for (SDL_Gamepad* pad : g_sdl_pads) SDL_CloseGamepad(pad);
    g_sdl_pads.clear();
  }
  SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
  g_sdl_initialized = false;
#endif
}

bool GameGetsInput() {
  if (AnyOpen()) return false;
  if (!g_wait_release) return true;
  const PadState pad = ReadPads();
  if (pad.buttons || pad.triggers || PanelKeysHeld()) return false;
  g_wait_release = false;
  return true;
}

}  // namespace fear::panel

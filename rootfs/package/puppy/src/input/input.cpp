#include "input/input.h"

#include <algorithm>
#include <cstdlib>

namespace {
constexpr Uint32 kInputPollMs       = 10;   // see waitEvent()
constexpr int kStickPress           = 16000; // left stick counts as a direction past about half travel...
constexpr int kStickRelease         = 8000;  // ...and lets go below about a quarter (axes span +-32767)
}

Action actionFromKey(SDL_Keycode k) {
    switch (k) {
        case SDLK_UP:     return Action::Up;
        case SDLK_DOWN:   return Action::Down;
        case SDLK_LEFT:   return Action::Left;
        case SDLK_RIGHT:  return Action::Right;
        case SDLK_RETURN: return Action::Launch;
        case SDLK_SPACE:  return Action::ToggleAutoStart;
        default:          return Action::None;
    }
}

bool isDirection(Action a) {
    return a == Action::Up || a == Action::Down || a == Action::Left || a == Action::Right;
}

Action actionFromButton(Uint8 b) {
    switch (b) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:    return Action::Up;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  return Action::Down;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  return Action::Left;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return Action::Right;
        case SDL_CONTROLLER_BUTTON_A:          return Action::Launch;
        case SDL_CONTROLLER_BUTTON_Y:          return Action::ToggleAutoStart;
        default:                               return Action::None;
    }
}

// Maps the left stick onto the same Up/Down/Left/Right actions as the d-pad. `current` is the
// direction the stick is already holding (or None). A held direction is kept until the stick falls
// back towards centre, and only changes on a deliberate push, so a stick that wobbles sideways while
// springing back doesn't produce a stray move.
Action actionFromStick(int x, int y, Action current) {
    int ax = std::abs(x), ay = std::abs(y);
    Action dir = ax >= ay ? (x < 0 ? Action::Left : Action::Right)
                          : (y < 0 ? Action::Up   : Action::Down);
    int pushed = std::max(ax, ay);
    if (isDirection(current)) {
        if (dir != current && pushed >= kStickPress) return dir;
        int along = current == Action::Left  ? -x
                  : current == Action::Right ?  x
                  : current == Action::Up    ? -y : y;
        return along >= kStickRelease ? current : Action::None;
    }
    return pushed >= kStickPress ? dir : Action::None;
}

// SDL_WaitEventTimeout() can't sleep until input arrives on KMSDRM (no WaitEventTimeout in the
// backend), so it polls every 1 ms. Polling every kInputPollMs keeps navigation instant and wakes
// the CPU 10x less while the launcher sits idle.
bool waitEvent(SDL_Event* ev, int timeoutMs) {
    const Uint32 end = SDL_GetTicks() + timeoutMs;
    for (;;) {
        if (SDL_PollEvent(ev)) return true;
        const Sint32 left = (Sint32)(end - SDL_GetTicks());
        if (left <= 0) return false;
        SDL_Delay(std::min<Uint32>(left, kInputPollMs));
    }
}

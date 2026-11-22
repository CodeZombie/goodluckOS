#include "catalog/catalog.h"
#include "domain/model.h"
#include "input/input.h"
#include "platform/platform.h"
#include "ui/ui.h"

#include <algorithm>
#include <iostream>

namespace {
constexpr Uint32 kIdleCheckMs       = 60000;
constexpr Uint32 kRepeatDelayMs     = 350;  // holding a direction repeats it after this...
constexpr Uint32 kRepeatRateMs      = 60;   // ...and then this often
}

int main() {
    Model model;
    model.categories = loadCatalog();
    if (model.categories.empty()) {
        std::cerr << "No entries found in apps.puppy files!\n";
        return 1;
    }

    restoreCursor(model);
    {
        std::string cat, name;
        if (readAutoStartId(cat, name)) model.find(cat, name, model.autoRow, model.autoCol);
    }

    Ui ui;
    if (!ui.ok()) return 1;

    SDL_GameController* pad = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); break; }
    }

    bool running = true;
    bool dirty = true;
    int shownBattery = -2;
    Uint32 lastActivity = SDL_GetTicks();
    Action held = Action::None;
    Uint32 nextRepeat = 0;
    int stickX = 0, stickY = 0;
    Action stickDir = Action::None;

    while (running) {
        if (dirty) {
            shownBattery = readBattery();
            ui.render(model, shownBattery);
            dirty = false;
        }

        Uint32 now = SDL_GetTicks();
        Uint32 elapsed = now - lastActivity;
        int timeout = elapsed >= kIdleCheckMs ? 0 : (int)(kIdleCheckMs - elapsed);
        if (held != Action::None)
            timeout = std::min(timeout, (int)std::max<Sint32>(0, (Sint32)(nextRepeat - now)));

        SDL_Event ev;
        if (!waitEvent(&ev, timeout)) {
            now = SDL_GetTicks();
            if (held != Action::None && (Sint32)(now - nextRepeat) >= 0) {
                ev.type = SDL_USEREVENT;
                nextRepeat = now + kRepeatRateMs;
            } else {
                lastActivity = now;
                if (readBattery() != shownBattery) dirty = true;
                continue;
            }
        }

        do {
            Action action = Action::None;
            switch (ev.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_WINDOWEVENT:
                    if (ev.window.event == SDL_WINDOWEVENT_EXPOSED) dirty = true;
                    break;
                case SDL_USEREVENT:
                    action = held;
                    break;
                case SDL_KEYDOWN:
                    if (!ev.key.repeat) action = actionFromKey(ev.key.keysym.sym);
                    break;
                case SDL_CONTROLLERBUTTONDOWN:
                    action = actionFromButton(ev.cbutton.button);
                    break;
                case SDL_CONTROLLERAXISMOTION: {
                    if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX)      stickX = ev.caxis.value;
                    else if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) stickY = ev.caxis.value;
                    else break;
                    Action dir = actionFromStick(stickX, stickY, stickDir);
                    if (dir != stickDir) {
                        if (held == stickDir) held = Action::None; // the stick was driving the repeat
                        stickDir = dir;
                        action = dir;  // None on release; a direction acts like a d-pad press
                    }
                    break;
                }
                case SDL_KEYUP:
                    if (actionFromKey(ev.key.keysym.sym) == held) held = Action::None;
                    break;
                case SDL_CONTROLLERBUTTONUP:
                    if (actionFromButton(ev.cbutton.button) == held) held = Action::None;
                    break;
            }

            if (ev.type != SDL_USEREVENT && isDirection(action)) {
                held = action;
                nextRepeat = SDL_GetTicks() + kRepeatDelayMs;
            }

            switch (action) {
                case Action::Up:    model.move(0, -1); break;
                case Action::Down:  model.move(0, 1);  break;
                case Action::Left:  model.move(-1, 0); break;
                case Action::Right: model.move(1, 0);  break;
                case Action::ToggleAutoStart: toggleAutoStart(model); break;
                case Action::Launch:
                    held = Action::None;
                    if (const Entry* e = model.selected()) {
                        launch(*e);
                    }
                    break;
                case Action::None: break;
            }

            if (action != Action::None) {
                dirty = true;
                lastActivity = SDL_GetTicks();
            }
        } while (running && SDL_PollEvent(&ev));
    }

    if (pad) SDL_GameControllerClose(pad);
    return 0;
}

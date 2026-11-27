#pragma once

#include <SDL2/SDL.h>

enum class Action { None, Up, Down, Left, Right, Launch, ToggleAutoStart };

Action actionFromKey(SDL_Keycode key);
Action actionFromButton(Uint8 button);
Action actionFromStick(int x, int y, Action current);
bool isDirection(Action action);
bool waitEvent(SDL_Event* event, int timeoutMs);

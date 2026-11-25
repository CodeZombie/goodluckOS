// MenUI - A dead-simple menu application library for mobile devices
// Copyright (c) 2026 Jeremy Clark (https://github.com/CodeZombie/)
// SPDX-License-Identifier: MIT

#pragma once
#include <stdbool.h>

extern "C" {
    // NOTE: if you already have these enum values in your own header, keep yours
    // and only make sure the names below exist.
    enum MenuiInput {
        INPUT_NONE = 0,   // "nothing happened, but a frame is due" (animation / redraw)
        INPUT_QUIT,
        INPUT_UP,
        INPUT_DOWN,
        INPUT_LEFT,
        INPUT_RIGHT,
        INPUT_A,          // confirm
        INPUT_B,          // cancel / back
        INPUT_X,
        INPUT_Y,
    };

    enum MenuiAlign {
        MENUI_ALIGN_LEFT = 0,   // [label] ............ [icon | value box]
        MENUI_ALIGN_CENTER,     //        [icon][label]                      (no value box allowed)
        MENUI_ALIGN_RIGHT,      // [icon | value box] ............ [label]
    };

    // One row of the menu. All pointers are only read during Menui_Row(); the
    // library copies whatever it needs, so Lua-owned / temporary strings are fine.
    struct MenuiRow {
        // Optional. nullptr or "" => the label takes up 0 width and 0 height.
        const char* label = nullptr;
        MenuiAlign  label_align = MENUI_ALIGN_LEFT;

        // Optional "item" that sits opposite the label (see MenuiAlign).
        //   item_outline == false : `item` is an icon. Only its first UTF-8
        //                           codepoint is used (usually an emoji).
        //   item_outline == true  : `item` is a value, drawn inside an outlined
        //                           box (wraps up to ~49% of the row width).
        //                           nullptr / "" draws an empty box.
        // With MENUI_ALIGN_CENTER an outlined item is not allowed and is dropped
        // (a warning is logged once). Icons are fine and sit left of the label.
        const char* item = nullptr;
        bool        item_outline = false;

        bool interactive = false;   // shows the accent bar when also selected
        bool selected = false;
    };

    // `font_paths[0]` is the primary font (hard failure if it can't be loaded).
    // All others are fallbacks, tried in order (a failure is only a warning).
    bool Menui_Init(const char* window_title, const char* font_path);
    void Menui_Quit();
    bool Menui_IsRunning();

    bool Menui_LoadFallbackFont(const char* font_path, const float scale);


    // Blocks until there's input, OR a frame is due (animation / redraw pending),
    // in which case INPUT_NONE is returned. After *every* return, draw a frame.
    MenuiInput Menui_WaitForInput();

    void Menui_BeginFrame(const char* header_text);
    void Menui_Row(const MenuiRow* row);
    void Menui_SetFooterText(const char* text);   // call between Begin/EndFrame; resets every frame
    void Menui_SetHeaderText(const char* text);   // call between Begin/EndFrame; resets every frame
    void Menui_EndFrame();

    // Optional knobs.
    void Menui_SetSwapConfirmCancel(bool swap);   // false: SOUTH=A(confirm) EAST=B(cancel). true: swapped.
    void Menui_SetDebugOverlay(bool enabled);     // on by default
}

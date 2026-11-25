-- MenUI - A dead-simple menu application library for mobile devices
-- Copyright (c) 2026 Jeremy Clark (https://github.com/CodeZombie/)
-- SPDX-License-Identifier: MIT

local ffi = require("ffi")

ffi.cdef[[
    typedef enum { INPUT_NONE = 0, INPUT_QUIT, INPUT_UP, INPUT_DOWN, INPUT_LEFT, INPUT_RIGHT, INPUT_A, INPUT_B, INPUT_X, INPUT_Y } MenuiInput;
    typedef enum { MENUI_ALIGN_LEFT = 0, MENUI_ALIGN_CENTER, MENUI_ALIGN_RIGHT } MenuiAlign;
    typedef struct {
        const char* label;
        MenuiAlign  label_align;
        const char* item;
        bool        item_outline;
        bool interactive;
        bool selected;
    } MenuiRow;
    bool Menui_Init(const char* window_title, const char* font_path);
    void Menui_Quit();
    bool Menui_IsRunning();
    bool Menui_LoadFallbackFont(const char* font_path, const float scale);
    MenuiInput Menui_WaitForInput();
    void Menui_BeginFrame();
    void Menui_Row(const MenuiRow* row);
    void Menui_SetFooterText(const char* text);
    void Menui_SetHeaderText(const char* text);
    void Menui_EndFrame();
    void Menui_SetSwapConfirmCancel(bool swap);
    void Menui_SetDebugOverlay(bool enabled);
]]

local lib = ffi.load("menui") -- "this resolves to /usr/lib/libmenu.so"

local menui = {
    auto_exit = true
}

-- Internal State
local page_stack = {}
local elements = {}

local frame_footer_text = ""

menui.GAMEPAD_GLYPHS = {
    A = "⇓",
    B = "⇒",
    X = "⇐",
    Y = "⇑",
    DPAD_HORIZONTAL = "↢",
    DPAD_VERTICAL = "↣",
    DPAD_UP = "↟",
    DPAD_DOWN = "↡",
    DPAD_LEFT = "↞",
    DPAD_RIGHT = "↠",
    START = "⇸",
    SELECT = "⇷",
    FN = "∌",
    L1 = "↰",
    L2 = "↲",
    R1 = "↱",
    R2 = "↳"
}


-- Local functions

function table.indexOf(t, object)
    if not t then return nil end
    for i = 1, #t do
        if t[i] == object then
            return i
        end
    end
    return nil
end

local function get_current_page()
    return page_stack[#page_stack]
end

local function get_selected_element()
    return elements[get_current_page()._element_index]
end

local function is_selected(element)
    local selected_element = get_selected_element()
    if selected_element == nil then return false end
    if selected_element.id == element.id then return true end
    return false
end

local function fail_if_id_not_unique(id)
    for _, e in ipairs(elements) do
        if id == e.id then
            error(string.format("Duplicate element ID detected: %s", id))
        end
    end
end

local function get_alginment_from_string(alignment_string)
    local alignment_string_lower = string.lower(alignment_string or "")
    if alignment_string_lower == "right" then
        return lib.MENUI_ALIGN_RIGHT
    elseif alignment_string_lower == "center" or alignment_string_lower == "centre" then
        return lib.MENUI_ALIGN_CENTER
    end
    return lib.MENUI_ALIGN_LEFT
end

function menui:push_page(page_id, context)
    -- clear the `on_enter_executed` flag for all pages in the page_stack
    for _, v in pairs(page_stack) do
        v._on_enter_executed = false
    end

    local page = {
        id = page_id,
        _on_enter_executed = false,
        _element_index = 1,
    }
    -- insert all the values from the context into the page data
    if context then
        for k, v in pairs(context) do
            page[k] = v
        end
    end

    table.insert(page_stack, page)
end

function menui:pop_page()
    if #page_stack > 1 then
        table.remove(page_stack)
        return
    end
    if menui.on_exit then menui.on_exit() end
    if menui.auto_exit then
        lib.Menui_Quit()
    end
end

function menui:page(id, page_params)
    if not id then
        error("page{} must be called with an id", 0)
    end
    page_params.id = id

    if #page_stack == 0 then menui:push_page(page_params.id) end

    local current_page = page_stack[#page_stack]
    if current_page and current_page.id == page_params.id then
        for k, v in pairs(page_params) do
            current_page[k] = v
        end
        if page_params.on_enter and not current_page._on_enter_executed then
            page_params.on_enter(current_page)
            current_page._on_enter_executed = true
        end
        page_params.on_render(current_page)
    end
end

local function abstract_element(id, element_params)
    if not id then
        error("Element must be defined with an id", 0)
    end
    fail_if_id_not_unique(id)
    element_params.id = id
    table.insert(elements, element_params)

    if element_params.label_icon then
        element_params.label = string.format("%s    %s", element_params.label_icon, element_params.label)
    end

    local interactive = false
    local selected = is_selected(element_params)

    if selected and element_params.footer_text then
        frame_footer_text = frame_footer_text .. element_params.footer_text .. " "
    end
    if selected then
        if element_params.on_change then
            frame_footer_text = frame_footer_text .. string.format("  %sChange", menui.GAMEPAD_GLYPHS.DPAD_HORIZONTAL)
            if element_params.default then
                frame_footer_text = frame_footer_text .. string.format("  %sReset", menui.GAMEPAD_GLYPHS.A)
            end
        end

        if element_params.on_a_press then
            frame_footer_text = frame_footer_text .. string.format("  %s%s", menui.GAMEPAD_GLYPHS.A, element_params.on_a_press_description or "Interact")
        end

        if element_params.on_x_press then
            frame_footer_text = frame_footer_text .. string.format("  %s%s", menui.GAMEPAD_GLYPHS.X, element_params.on_x_press_description or "Interact")
        end

        if element_params.on_y_press then
            frame_footer_text = frame_footer_text .. string.format("  %s%s", menui.GAMEPAD_GLYPHS.Y, element_params.on_y_press_description or "Interact")
        end
    end

    if element_params.on_change or element_params.on_a_press or element_params.on_x_press or element_params.on_y_press then
        interactive = true
    end

    return selected, interactive
end

-- Element wrappers
function menui:label(id, element_params)
    local selected, interactive = abstract_element(id, element_params)

    lib.Menui_Row(ffi.new("MenuiRow", {
        label = element_params.label,
        label_align = get_alginment_from_string(element_params.align),
        interactive = interactive,
        selected = selected,
        item = element_params.icon
    }))
    -- lib.Menui_Label(element.label or element.name, false, selected, element.icon)
end

function menui:select(id, element_params)
    local selected, interactive = abstract_element(id, element_params)

    if element_params.on_change then
        element_params.on_button_left = function()
            local index = table.indexOf(element_params.options, element_params.value)
            if index then
                if index + 1 > #element_params.options then
                    index = 1
                else
                    index = index + 1
                end
                element_params.on_change(index, element_params.options[index])
            end
        end
        element_params.on_button_right = function()
            local index = table.indexOf(element_params.options, element_params.value)
            if index then
                if index - 1 < 1 then
                    index = #element_params.options
                else
                    index = index - 1
                end
                element_params.on_change(index, element_params.options[index])
            end
        end
        if element_params.default then
            element_params.on_button_a = function()
                local index = table.indexOf(element_params.options, element_params.default)
                if index then
                    element_params.on_change(index, element_params.default)
                end
            end
        end
    end

    if not element_params.value_string_format then element_params.value_string_format = "%s" end

    lib.Menui_Row(ffi.new("MenuiRow", {
        label = element_params.label,
        label_align = get_alginment_from_string(element_params.align),
        interactive = interactive,
        selected = selected,
        item = string.format(element_params.value_string_format, element_params.value),
        item_outline = true
    }))
end

function menui:number(id, element_params)
    local selected, interactive = abstract_element(id, element_params)

    if element_params.on_change then
        element_params.on_button_left = function()
            local new_value = element_params.value - (element_params.step or 1)
            if element_params.min and new_value <= element_params.min then
                if element_params.max then
                    new_value = element_params.max
                else
                    return
                end
            end
            element_params.on_change(new_value)
        end
        element_params.on_button_right = function()
            local new_value = element_params.value + (element_params.step or 1)
            if element_params.max and new_value >= element_params.max then
                if element_params.min then
                    new_value = element_params.min
                else
                    return
                end
            end
            element_params.on_change(new_value)
        end
        if element_params.default then
            element_params.on_button_a = function()
                element_params.on_change(element_params.default)
            end
        end
    end

    if not element_params.value_string_format then element_params.value_string_format = "%s" end
    lib.Menui_Row(ffi.new("MenuiRow", {
        label = element_params.label,
        label_align = get_alginment_from_string(element_params.align),
        interactive = interactive,
        selected = selected,
        item = string.format(element_params.value_string_format, element_params.value),
        item_outline = true
    }))
end

function menui:bool(id, element_params)
    local selected, interactive = abstract_element(id, element_params)

    if not element_params.true_string then element_params.true_string = "True" end
    if not element_params.false_string then element_params.false_string = "False" end

    if element_params.on_change then
        element_params.on_button_left = function()
            element_params.on_change(not element_params.value)
        end
        element_params.on_button_right = function()
            element_params.on_change(not element_params.value)
        end
        if element_params.default then
            element_params.on_button_a = function()
                element_params.on_change(element_params.default)
            end
        end
    end

    if not element_params.value_string_format then element_params.value_string_format = "%s" end
    lib.Menui_Row(ffi.new("MenuiRow", {
        label = element_params.label,
        label_align = get_alginment_from_string(element_params.align),
        interactive = interactive,
        selected = selected,
        item = string.format(element_params.value_string_format, tostring(element_params.value and element_params.true_string or element_params.false_string)),
        item_outline = true
    }))
end

function menui:button(id, element_params)
    local selected, interactive = abstract_element(id, element_params)

    lib.Menui_Row(ffi.new("MenuiRow", {
        label = element_params.label,
        label_align = get_alginment_from_string(element_params.align),
        interactive = interactive,
        selected = selected,
        item = element_params.icon
    }))
end

function menui:start(window_title, app_logic)
    if not window_title then
        error("Menui:start() must be called with a window title")
    end

    if not lib.Menui_Init(window_title, "/usr/share/fonts/noto/NotoSans.ttf") then
        print("Failed to initialize Menui!")
        return
    end
    if not lib.Menui_LoadFallbackFont("/usr/share/fonts/prompt-font/promptfont.ttf", 1.2) then
        print("Failed to load fallback emoji font")
    end
    if not lib.Menui_LoadFallbackFont("/usr/share/fonts/noto/NotoEmoji.ttf", 1.25) then
        print("Failed to load fallback emoji font")
    end
    local last_input = nil

    while lib.Menui_IsRunning() do
        if last_input ~= nil then
            last_input = lib.Menui_WaitForInput()
        end

        if last_input == lib.INPUT_QUIT then
            lib.Menui_Quit()
            break
        end
        local current_page = get_current_page()
        if last_input == lib.INPUT_UP then
            current_page._element_index = current_page._element_index - 1
            if current_page._element_index < 1 then current_page._element_index = #elements end
        elseif last_input == lib.INPUT_DOWN then
            current_page._element_index = current_page._element_index + 1
            if current_page._element_index > #elements then current_page._element_index = 1 end
        elseif last_input == lib.INPUT_LEFT then
            local element = get_selected_element()
            if element and element.on_button_left then element.on_button_left() end
        elseif last_input == lib.INPUT_RIGHT then
            local element = get_selected_element()
            if element and element.on_button_right then element.on_button_right() end
        elseif last_input == lib.INPUT_A then
            local element = get_selected_element()
            if element and element.on_a_press then element.on_a_press() end
        elseif last_input == lib.INPUT_X then
            local element = get_selected_element()
            if element and element.on_x_press then element.on_x_press() end
        elseif last_input == lib.INPUT_Y then
            local element = get_selected_element()
            if element and element.on_y_press then element.on_y_press() end
        elseif last_input == lib.INPUT_B then
            menui:pop_page()
        end

        lib.Menui_BeginFrame()
        frame_footer_text = ""

        -- Execute user logic (this calls the menui:page blocks)
        elements = {}
        app_logic()

        current_page = get_current_page()
        local frame_header_text
        if current_page then
            frame_header_text = current_page.label or current_page.id
        else
            frame_header_text = "No Page"
        end
        lib.Menui_SetHeaderText(frame_header_text)

        -- Footer Text
        frame_footer_text = frame_footer_text .. string.format("  %sNavigate", menui.GAMEPAD_GLYPHS.DPAD_VERTICAL)
        if #page_stack > 1 then
            frame_footer_text = string.format("%s  %s%s", frame_footer_text, menui.GAMEPAD_GLYPHS.B, "Back")
        elseif menui.auto_exit then
            frame_footer_text = string.format("%s  %s%s", frame_footer_text, menui.GAMEPAD_GLYPHS.B, "Exit")
        end
        lib.Menui_SetFooterText(frame_footer_text)

        lib.Menui_EndFrame()
        last_input = lib.INPUT_NONE
    end

    lib.Menui_Quit()
end

function menui:quit()
    lib.Menui_Quit()
end

return menui

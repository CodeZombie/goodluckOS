-- libsettings
-- The I/O backbone of shared system settings not already covered by another application.
-- If you want to read or write a system setting, you will do so through this script.
--
-- NOTE: if you're writing non-lua code and want to interact with this library, you can use the
-- `settings` application to read and write settings. See `/usr/local/bin/settings`.
--

local MIXER      = "Speaker"                       -- ALSA simple mixer control
local BACKLIGHT  = "/sys/class/backlight/backlight"
local CPU_GLOB   = "/sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_governor"
local APPD       = "/usr/local/bin/appd"
local APP_CACHE = "/usr/local/bin/app-cache"
local GPU_MODPROBE_CONF          = "/etc/modprobe.d/gpu-driver.conf"
local LAUNCHER_CONF = "/var/lib/goodluckos/launcher"
local LOW_BATTERY_THRESHOLD_CONF = "/var/lib/goodluckos/low-battery-threshold"
local GPU_DRIVER_CONF = "/var/lib/goodluckos/gpu-driver"
local LOCALE_PROFILED_SCRIPT = "/etc/profile.d/locale.sh"

local MONTHS = {
    "january",
    "february",
    "march",
    "april",
    "may",
    "june",
    "july",
    "august",
    "september",
    "october",
    "november",
    "december",
}

local LANGUAGES = {
    english = { "English", "English", "en_US", "en" },
    mandarin_chinese = { "Mandarin Chinese", "普通话", "zh_Hans", "zh" },
    hindi = { "Hindi", "हिन्दी", "hi_IN", "hi" },
    spanish = { "Spanish", "Español", "es_ES", "es" },
    modern_standard_arabic = { "Modern Standard Arabic", "العربية الفصحى", "ar_001", "arb" },
    french = { "French", "Français", "fr_FR", "fr" },
    bengali = { "Bengali", "বাংলা", "bn_BD", "bn" },
    portuguese = { "Portuguese", "Português", "pt_BR", "pt" },
    indonesian = { "Indonesian", "Bahasa Indonesia", "id_ID", "id" },
    urdu = { "Urdu", "اردو", "ur_PK", "ur" },
    russian = { "Russian", "Русский", "ru_RU", "ru" },
    standard_german = { "Standard German", "Deutsch", "de_DE", "de" },
    japanese = { "Japanese", "日本語", "ja_JP", "ja" },
    nigerian_pidgin = { "Nigerian Pidgin", "Naijá", "pcm_NG", "pcm" },
    egyptian_arabic = { "Egyptian Arabic", "مصري", "ar_EG", "arz" },
    marathi = { "Marathi", "मराठी", "mr_IN", "mr" },
    vietnamese = { "Vietnamese", "Tiếng Việt", "vi_VN", "vi" },
    telugu = { "Telugu", "తెలుగు", "te_IN", "te" },
    swahili = { "Swahili", "Kiswahili", "sw_KE", "sw" },
    hausa = { "Hausa", "Harshen Hausa", "ha_NG", "ha" }
}

local ORDERED_LANGUAGE_ENGLISH_NAMES = {}
for _, lang in pairs(LANGUAGES) do
    table.insert(ORDERED_LANGUAGE_ENGLISH_NAMES, lang[1])
end

table.sort(ORDERED_LANGUAGE_ENGLISH_NAMES)

-- # Helpers
local BOOL_TRUE  = { ["true"] = true, ["1"] = true, ["on"] = true,  ["yes"] = true }
local BOOL_FALSE = { ["false"] = true, ["0"] = true, ["off"] = true, ["no"] = true }

local function isfloat(x)
    return type(x) == "number" and x % 1 ~= 0
end

local function parse_value(def, raw)
    local parsed_value = raw
    if def.type == "int" then
        if isfloat(parsed_value) then
            parsed_value = math.floor(parsed_value)
        else
            parsed_value = tonumber(parsed_value)
            if parsed_value == nil then
                return nil, string.format("%s: \"%s\" could not be used as an integer", def.id, tostring(raw))
            end
        end
        if parsed_value < def.min or parsed_value > def.max then
            return nil, string.format("%s: %d out of range (%d..%d)", def.id, parsed_value, def.min, def.max)
        end
        return parsed_value, nil
    elseif def.type == "float" then
        parsed_value = tonumber(parsed_value)
        if parsed_value == nil then
            return nil, string.format("%s: \"%s\" could not be used as a float", def.id, tostring(raw))
        end
        if parsed_value < def.min or parsed_value > def.max then
            return nil, string.format("%s: %d out of range (%d..%d)", def.id, parsed_value, def.min, def.max)
        end
        return parsed_value, nil
    elseif def.type == "string" then
        return tostring(parsed_value), nil
    elseif def.type == "select" then
        local l = raw:lower()
        for _, v in ipairs(def.options) do
            if v:lower() == l then return v, nil end
        end
        return nil, string.format("%s: \"%s\" is not one of: %s", def.id, raw, tostring(table.concat(def.options, ", ")))
    elseif def.type == "bool" then
        if type(raw) == "boolean" then
            return raw, nil
        end
        local l = raw:lower()
        if BOOL_TRUE[l] then return true, nil end
        if BOOL_FALSE[l] then return false, nil end
        return nil, string.format("%s: \"%s\" is not a boolean (true/false", def.id, tostring(raw))
    end
    return raw, nil
end

local function trim(s)
    return (tostring(s or ""):gsub("^%s+", ""):gsub("%s+$", ""))
end

local function shell_quote(s)
    return "'" .. (tostring(s):gsub("'", "'\\''")) .. "'"
end

local function run(cmd) -- true on exit status 0 (works for 5.1 and 5.2 semantics)
    local r = os.execute(cmd .. " >/dev/null 2>&1")
    return r == true or r == 0
end

local function capture(cmd)
    local p = io.popen(cmd .. " 2>/dev/null")
    if not p then return nil end
    local out = p:read("*a")
    p:close()
    return out
end

local function read_file(path, default_value)
    local f = io.open(path, "r")
    if not f then return default_value end
    local s = f:read("*a")
    f:close()
    return s
end

local function write_file(path, data, mode)
    if not mode then mode = "w" end
    local f, err = io.open(path, "w")
    if not f then error("cannot write " .. path .. ": " .. tostring(err), 0) end
    f:write(data)
    f:close()
end

local function split_tokens(s)
    local out = {}
    for tok in tostring(s or ""):gmatch("[^%s,]+") do out[#out + 1] = tok end
    return out
end

---------------------------------------------------------------------------
-- Setting implementations
---------------------------------------------------------------------------
local function alsa_state()
  local out = capture("amixer sget " .. shell_quote(MIXER))
  if not out or out == "" then error("amixer: cannot read control '" .. MIXER .. "'", 0) end
  return out
end

local function alsa_volume()
    local out = capture(string.format("amixer sget %s | grep -o '[0-9]*' | head -1", shell_quote(MIXER)))
    if not out or out == "" then error("amixer: cannot read volume from '" .. MIXER .. "'", 0) end
    return out
end

local function alsa_store() run("alsactl store") end

-- clock helpers


local function clock_get(idx)
  local out = capture("date '+%Y %m %d %H %M %S'")
  local t = {}
  for n in tostring(out):gmatch("%d+") do t[#t + 1] = tonumber(n) end
  if #t ~= 6 then error("cannot read system time via date(1)", 0) end
  return t[idx], t
end

local function clock_set(idx, value)
  local _, t = clock_get(idx)
  t[idx] = value
  local str = string.format("%04d-%02d-%02d %02d:%02d:%02d", unpack(t))
  if not run("date -s " .. shell_quote(str)) then error("could not set date/time to " .. str, 0) end
  run("hwclock --systohc")
end

local function month_index(name)
  for i, m in ipairs(MONTHS) do if m == name then return i end end
end

local SETTINGS = {
    volume = {
        id = "volume",
        name = "Volume",
        type = "int",
        min = 0,
        max = 100,
        get = function()
            local out = capture(string.format("amixer sget %s | grep -o '[0-9]*' | head -1", shell_quote(MIXER)))
            if not out or out == "" then error("amixer: cannot read volume from '" .. MIXER .. "'", 0) end
            return tonumber(out)
        end,
        set = function(self, value)
            local parsed_value, err = parse_value(self, value)
            if err then error(err, 0) end

            if not run("amixer sset " .. shell_quote(MIXER) .. " " .. parsed_value .. "%") then error("amixer failed to set volume", 0) end
            alsa_store()
            return parsed_value, nil
        end,
    },
    mute = {
        id = "mute",
        name = "Mute",
        type = "bool",
        get = function(self)
            local out = capture(string.format("amixer sget Speaker | grep -oE '\\[(on|off)\\]'", shell_quote(MIXER)))
            if not out or out == "" then error("amixer: cannot read mute state from '" .. MIXER .. "'", 0) end
            return out == "[off]"
        end,
        set = function(self, value)
            local parsed_value, err = parse_value(self, value)
            if err then error(err, 0) end

            if not run("amixer sset " .. shell_quote(MIXER) .. " " .. (parsed_value and "mute" or "unmute")) then
                error("amixer failed to change mute state", 0)
            end
            alsa_store()
            return parsed_value, nil
        end,
    },
    brightness = {
        id = "brightness",
        name = "Brightness",
        type = "int",
        min = 1,
        max = tonumber(trim(read_file(BACKLIGHT .. "/max_brightness", 10))),
        get = function(self)
            return tonumber(trim(read_file(BACKLIGHT .. "/brightness", "5")))
        end,
        set = function(self, value)
            local parsed_value, err = parse_value(self, value)
            if err then error(err, 0) end
            write_file(BACKLIGHT .. "/brightness", tostring(parsed_value))
            return parsed_value, nil
        end,
    },
    battery = {
        id = "battery",
        name = "Battery",
        type = "int",
        get = function(self)
            local path = "/sys/class/power_supply/axp20x-battery/capacity"
            local file, _ = io.open(path, "r")

            if not file then
                error("No battery present", 0)
                return nil
            end

            local content = file:read("*a")
            file:close()

            if not content then
                error("Failed to read battery capacity", 0)
                return nil
            end

            local capacity = tonumber(content)
            if not capacity then
                error("Invalid battery capacity number format: " .. content, 0)
                return nil
            end

            return capacity
        end,
    },
    low_battery_alert = {
        id = "low-battery-alert",
        name = "Low Battery Alert",
        type = "int",
        min = 1,
        max = 50,
        get = function()
            return tonumber(trim(read_file(LOW_BATTERY_THRESHOLD_CONF, "15")))
        end,
        set = function(self, value)
            local parsed_value, err = parse_value(self, value)
            if err then error(err, 0) end
            write_file(LOW_BATTERY_THRESHOLD_CONF, parsed_value)
            return parsed_value, nil
        end,
    },
    language = {
        id = "language",
        name = "Language",
        type = "select",
        options = { "english", "chinese", "hindi", "spanish", "arabic", "french", "bengali", "portuguese", "indonesian", "urdu", "russian", "german", "japanese", "nigerian pidgin", "egyptian arabic", "marathi", "Vietnamese", "Telugu", "Swahili", "Hausa" },
        get = function(self)
            local locale_script_raw = read_file(LOCALE_PROFILED_SCRIPT, nil)
            if not locale_script_raw then return nil, "Could not read locale file at " .. LOCALE_PROFILED_SCRIPT end
            local lang_code = locale_script_raw:match("[1]LANG=['\"]?([%a_]+)")
            if not lang_code then return nil, "Could not parse locale file at " .. LOCALE_PROFILED_SCRIPT end

            for k, v in pairs(LANGUAGES) do
                if lang_code == v[3] then
                    return v[1]
                end
            end
            return nil, string.format("Could not identify language by the LANG code \"%s\" stored in %s", lang_code, LOCALE_PROFILED_SCRIPT)
        end,
        set = function(self, value)
            local parsed_value, err = parse_value(self, value)
            if err then return nil, err end

            parsed_value = parsed_value:lower():gsub(" ", "_"):gsub("-", "_")

            local selected_language = LANGUAGES[parsed_value]
            if not selected_language then
                return nil, string.format("Unsupported language %s", value)
            end
            write_file(LOCALE_PROFILED_SCRIPT, string.format("export LANG=%s.UTF-8\nexport LC_ALL=%s.UTF-8", LANGUAGES[parse_value][3], LANGUAGES[parse_value][3]))
            return parsed_value, nil
        end,
    },
    cpu_governor = {
        id = "cpu-governor",
        name = "CPU Governor",
        type = "select",
        options = (function()
            local avail = trim(read_file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_governors", "ondemand"))
            local list = split_tokens(avail)
            if #list == 0 then list = { "performance", "ondemand", "powersave" } end
            return list
        end)(),
        get = function(self)
            local first = capture("ls " .. CPU_GLOB):match("[^\n]+")
            if not first then error("no cpufreq support found", 0) end
            return trim(read_file(first, nil))
        end,
        set = function(self, value)
            local parsed_value, err = parse_value(self, value)
            if err then error(err, 0) end

            local any = false
            for path in capture("ls " .. CPU_GLOB):gmatch("[^\n]+") do
                write_file(path, parsed_value)
                any = true
            end
            if not any then
                error("no cpufreq support found", 0)
            end
            return parsed_value, nil
        end,
    },
    gpu_driver = {
        id = "gpu-driver",
        name = "GPU Driver",
        type = "select",
        options = { "lima", "mali" },
        get = function(self)
            local mods = read_file("/proc/modules") or ""
            for _, d in ipairs({ "lima", "mali" }) do
                if mods:match("^" .. d .. "%s") or mods:match("\n" .. d .. "%s") then return d end
            end
            return read_file(GPU_DRIVER_CONF, "lima")
        end,
        set = function(self, value)
            local parsed_value, err = parse_value(self, value)
            if err then error(err, 0) end
            local other = (parsed_value == "lima") and "mali" or "lima"
            write_file(GPU_MODPROBE_CONF, "blacklist " .. other .. "\n")
            write_file(GPU_DRIVER_CONF, parsed_value)
            return parsed_value, nil
        end,
    },
    time_second = {
        id = "time-second",
        name = "Time Second",
        type = "int",
        min = 0,
        max = 59,
        get = function(self) return (clock_get(6)) end,
        set = function(self, value)
            local parsed_value, err = parse_value(self, value)
            if err then error(err, 0) end
            clock_set(6, parsed_value)
            return parsed_value, nil
        end,
    },
    time_minute = {
        id = "time-minute",
        name = "Time Minute",
        type = "int",
        min = 0,
        max = 59,
        get = function(self) return (clock_get(5)) end,
        set = function(self, value)
            local parsed_value, err = parse_value(self, value)
            if err then error(err, 0) end
            clock_set(5, parsed_value)
            return parsed_value, nil
        end,
    },
    time_hour = {
        id = "time-hour",
        name = "Time Hour",
        type = "int",
        min = 0,
        max = 23,
        get = function(self) return (clock_get(4)) end,
        set = function(self, value)
            local parsed_value, err = parse_value(self, value)
            if err then error(err, 0) end
            clock_set(4, parsed_value)
            return parsed_value, nil
        end,
    },
    date_day = {
        id = "date-day",
        name = "Date Day",
        type = "int",
        min = 1,
        max = 31,
        get = function(self) return (clock_get(3)) end,
        set = function(self, value)
            local parsed_value, err = parse_value(self, value)
            if err then error(err, 0) end
            clock_set(3, parsed_value)
            return parsed_value, nil
        end,
    },
    date_month = {
        id = "date-month",
        name = "Date Month",
        type = "select",
        options = MONTHS,
        get = function(self) return MONTHS[(clock_get(2))] end,
        set = function(self, value)
            local parsed_value, err = parse_value(self, value)
            if err then error(err, 0) end
            clock_set(2, month_index(parsed_value))
            return parsed_value, nil
        end,
    },
    date_year = {
        id = "date-year",
        name = "Date Year",
        type = "int",
        min = 1970,
        max = 9999,
        get = function(self) return (clock_get(1)) end,
        set = function(self, value)
            local parsed_value, err = parse_value(self, value)
            if err then error(err, 0) end
            clock_set(1, parsed_value)
            return parsed_value, nil
        end,
    },
    launcher = {
        id = "launcher",
        name = "Launcher",
        type = "select",
        options = (function()
            local list = split_tokens(capture(APP_CACHE .. " list launchers"))
            if #list == 0 then return {"puppy"} end
            return list
        end)(),
        get = function(self)
            local cur = read_file(LAUNCHER_CONF, nil)
            if cur then return cur end
            local list = split_tokens(capture(APPD .. " launchers"))
            return list[1] or ""
        end,
        set = function(self, value)
            local parsed_value, err = parse_value(self, value)
            if err then error(err, 0) end
            write_file(LAUNCHER_CONF, parsed_value)
            return parsed_value, nil
        end,
    },
}

local function get(id)
    id = id:gsub('-', '_')
    return SETTINGS[id]:get()
end

local function set(id, value)
    id = id:gsub('-', '_') -- swap dashes for underscores
    if SETTINGS[id] == nil then
        return nil, string.format("Unknown setting \"%s\"", id)
    end
    if SETTINGS[id].set == nil then
        return nil, string.format("%s: read-only property", id)
    end
    return SETTINGS[id]:set(value)
end

return {
    get = get,
    set = set,
    settings = SETTINGS
}

-- Scanner for .puppy files ([ENTRY] and [ARCHIVE] sections).

local dirent = require("posix.dirent")
local pstat  = require("posix.sys.stat")

local ICON_EXTS = { png = true, jpg = true, jpeg = true, svg = true, webp = true }

local function log(msg) io.stderr:write("app-cache: puppy: ", msg, "\n") end
local function trim(s)  return (s:gsub("^%s+", ""):gsub("%s+$", "")) end

local function split(s, seps)
    local out = {}
    for part in (s or ""):gmatch("[^" .. (seps or ";") .. "]+") do
        part = trim(part)
        if part ~= "" then out[#out + 1] = part end
    end
    return out
end

local function shell_quote(s)
    return "'" .. s:gsub("'", "'\\''") .. "'"
end

local function is_regular_file(path)
    local st = pstat.stat(path)
    if not st then return false end
    local r = pstat.S_ISREG(st.st_mode)
    return r == true or (type(r) == "number" and r ~= 0)
end

-- Sorted file names in `dir`, or an empty list if unreadable.
local function list_dir(dir)
    local ok, names = pcall(dirent.dir, dir)
    if not ok then return {} end
    table.sort(names)
    return names
end

-- Parse an INI-style file into an ordered list of { section = "ENTRY", fields = {...} }.
-- Keys are lower-cased; section names are upper-cased. `[[X]]` is accepted as `[X]`.
local function parse_ini(path)
    local f = io.open(path, "rb")
    if not f then return nil end
    local data = f:read("*a") or ""
    f:close()
    data = data:gsub("^\239\187\191", "")   -- strip UTF-8 BOM

    local sections, cur = {}, nil
    for line in data:gmatch("[^\r\n]+") do
        line = trim(line)
        if line ~= "" and not line:match("^[#;]") then
            local name = line:match("^%[%[?(.-)%]%]?$")
            if name then
                cur = { section = trim(name):upper(), fields = {} }
                sections[#sections + 1] = cur
            elseif cur then
                local k, v = line:match("^([%w_%-%.]+)%s*=%s*(.*)$")
                if k then cur.fields[k:lower()] = v end
            end
        end
    end
    return sections
end

-- [ENTRY]/[LAUNCHER] -> one app. All keys pass through; app-cache decides which are
-- columns (name/command/description/category/icon) and which go to metadata.
local function from_entry(f, path)
    if not f.name or not f.command then
        log(path .. ": [ENTRY] needs NAME and COMMAND, skipping")
        return nil
    end
    local e = {}
    for k, v in pairs(f) do e[k] = v end
    return e
end

-- { basename-without-ext = icon path } across directories (first dir wins).
local function index_icons(dirs)
    local idx = {}
    for _, d in ipairs(dirs) do
        d = d:gsub("/+$", "")
        for _, file in ipairs(list_dir(d)) do
            local base, ext = file:match("^(.+)%.([^.]+)$")
            if base and ICON_EXTS[ext:lower()] and not idx[base] then
                idx[base] = d .. "/" .. file
            end
        end
    end
    return idx
end

-- [ARCHIVE] -> one app per matching file in ENTRY_DIRECTORIES.
local function from_archive(f, path)
    if not f.command or not f.entry_directories or not f.entry_extensions then
        log(path .. ": [ARCHIVE] needs COMMAND, ENTRY_DIRECTORIES and ENTRY_EXTENSIONS, skipping")
        return {}
    end

    local exts = {}
    for _, e in ipairs(split(f.entry_extensions, ";,")) do
        exts[(e:gsub("^%.", ""):lower())] = true
    end

    local icons    = index_icons(split(f.entry_icons_directories))
    local category = f.category or f.name
    local apps     = {}

    for _, dir in ipairs(split(f.entry_directories)) do
        dir = dir:gsub("/+$", "")
        for _, file in ipairs(list_dir(dir)) do
            local base, ext = file:match("^(.+)%.([^.]+)$")
            if base and exts[ext:lower()] then
                local rom = dir .. "/" .. file
                if is_regular_file(rom) then
                    local cmd, n = f.command:gsub("%%s", function() return shell_quote(rom) end)
                    if n == 0 then cmd = f.command .. " " .. shell_quote(rom) end

                    apps[#apps + 1] = {
                        name     = base,
                        command  = cmd,
                        category = category,
                        icon     = icons[base] or f.default_icon,
                        system   = f.name,   -- -> metadata
                        rom      = rom,      -- -> metadata
                    }
                end
            end
        end
    end
    return apps
end

local function scan(filepath)
    if not filepath:lower():match("%.puppy$") then return nil end

    local sections = parse_ini(filepath)
    if not sections then
        log("cannot read " .. filepath)
        return nil
    end

    local apps = {}
    for _, s in ipairs(sections) do
        if s.section == "ENTRY" then
            local e = from_entry(s.fields, filepath)
            if e then apps[#apps + 1] = e end
        elseif s.section == "ARCHIVE" then
            for _, e in ipairs(from_archive(s.fields, filepath)) do
                apps[#apps + 1] = e
            end
        end
    end
    return apps
end

return { scan = scan }

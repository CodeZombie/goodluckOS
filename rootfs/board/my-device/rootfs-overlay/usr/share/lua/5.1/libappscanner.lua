-- libappscanner: finds apps/roms by running scanner plugins over a set of files.
--
-- No database knowledge. It does two tightly linked things:
--   1. Location files: a directory of text files, each line a path or glob
--      ('#' starts a comment). Together they say *which files* to look at.
--   2. Scanners: Lua plugins that turn one file into zero or more app entries.
--
--   local libappscanner = require("libappscanner")
--
--   local scanner, err = libappscanner.new({
--       locations_dir = "/var/lib/goodluckos/app-cache/locations",
--       scanners_dir  = "/opt/goodluckos/app-cache/scanners",
--       warn = function(msg) io.stderr:write(msg, "\n") end,   -- optional
--   })
--   if not scanner then error(err) end
--
--   local results, stats = scanner.scan_all()
--   for _, r in ipairs(results) do
--       print(r.source, r.scanner, r.entry.name, r.entry.command)
--   end
--
-- Every result is { source = <file scanned>, scanner = <scanner filename>,
-- entry = <the table the scanner returned, untouched> }. Entries are not
-- validated or normalised here; that's up to whoever consumes them.
--
-- Scanner plugins
--   A scanner is a .lua file in scanners_dir that either returns
--   { scan = function(path) ... end } or defines a global `function scan(path)`.
--   scan(path) returns an array of entry tables (or nil if the file isn't
--   something it understands). Scanners run in a sandboxed global table that
--   falls back to _G, so one scanner's globals can't clobber another's.
--   Requires LuaJIT / Lua 5.1 (uses setfenv).

local pglob = require("posix.glob")

local M         = {
    LOCATIONS_DIR = "/var/lib/goodluckos/app-cache/locations",
    SCANNERS_DIR = "/opt/goodluckos/app-cache/scanners"
}

----------------------------------------------------------------------
-- helpers
----------------------------------------------------------------------
local function trim(s) return (s:gsub("^%s+", ""):gsub("%s+$", "")) end

-- Sorted list of paths matching a glob pattern (empty if none).
local function glob(pattern)
    return pglob.glob(pattern, 0) or {}
end

local function default_warn(msg)
    io.stderr:write("libappscanner: warning: ", msg, "\n")
end

----------------------------------------------------------------------
-- scanner loading
----------------------------------------------------------------------
local function load_scanners(dir, warn)
    local scanners = {}
    for _, path in ipairs(glob(dir .. "/*.lua")) do
        local chunk, err = loadfile(path)
        if not chunk then
            warn("cannot load scanner " .. path .. ": " .. tostring(err))
        else
            local env = setmetatable({}, { __index = _G })
            setfenv(chunk, env)
            local ok, ret = pcall(chunk)
            if not ok then
                warn("scanner " .. path .. " failed to initialise: " .. tostring(ret))
            else
                local fn
                if type(ret) == "table" and type(ret.scan) == "function" then
                    fn = ret.scan
                elseif type(rawget(env, "scan")) == "function" then
                    fn = env.scan
                end
                if fn then
                    scanners[#scanners + 1] = { name = path:match("([^/]+)$"), scan = fn }
                else
                    warn("scanner " .. path .. " has no scan(filepath) function")
                end
            end
        end
    end
    return scanners
end

----------------------------------------------------------------------
-- location files -> concrete file list
----------------------------------------------------------------------
local function collect_location_files(dir)
    local files, seen = {}, {}
    for _, lf in ipairs(glob(dir .. "/*")) do
        local f = io.open(lf, "r")
        if f then
            local data = f:read("*a") or ""
            f:close()
            for line in data:gmatch("[^\r\n]+") do
                line = trim(line)
                if line ~= "" and not line:match("^#") then
                    for _, p in ipairs(glob(line)) do
                        if not seen[p] then
                            seen[p] = true
                            files[#files + 1] = p
                        end
                    end
                end
            end
        end
    end
    return files
end

----------------------------------------------------------------------
-- public API
----------------------------------------------------------------------
-- opts.locations_dir  directory of location files (needed for scan_all/files)
-- opts.scanners_dir   directory of scanner plugins (needed unless opts.scanners)
-- opts.scanners       alternative to scanners_dir: array of { name=, scan=function(path) }
-- opts.warn           function(msg) called for non-fatal problems
--                     (default: stderr; pass function() end to silence)
--
-- Returns a scanner object, or nil, err (e.g. when no scanner could be loaded).
function M.new(opts)
    opts = opts or {}
    local warn = opts.warn or default_warn

    local scanners
    if opts.scanners then
        scanners = opts.scanners
    elseif M.SCANNERS_DIR then
        scanners = load_scanners(M.SCANNERS_DIR, warn)
        if #scanners == 0 then return nil, "no usable scanners in " .. M.SCANNERS_DIR end
    else
        return nil, "libappscanner.new: need scanners_dir or scanners"
    end

    local self = {}

    -- Names of the loaded scanners.
    function self.scanner_names()
        local names = {}
        for i, sc in ipairs(scanners) do names[i] = sc.name end
        return names
    end

    -- Concrete, de-duplicated list of files named by the location files.
    function self.files()
        if not M.LOCATIONS_DIR then return nil, "no locations_dir configured" end
        return collect_location_files(M.LOCATIONS_DIR)
    end

    -- Runs every scanner over one file. A scanner that errors is skipped with
    -- a warning. Returns an array of results (possibly empty), or nil, err if
    -- the file can't be read.
    function self.scan_file(path)
        local f = io.open(path, "rb")
        if not f then return nil, "cannot read " .. tostring(path) end
        f:close()

        local results = {}
        for _, sc in ipairs(scanners) do
            local ok, res = pcall(sc.scan, path)
            if not ok then
                warn(sc.name .. " failed on " .. path .. ": " .. tostring(res))
            elseif type(res) == "table" then
                for _, app in ipairs(res) do
                    if type(app) == "table" then
                        results[#results + 1] = { source = path, scanner = sc.name, app = app }
                    else
                        warn(sc.name .. " returned a non-table app entry for " .. path)
                    end
                end
            elseif res ~= nil then
                warn(sc.name .. " returned " .. type(res) .. " (expected a table) for " .. path)
            end
        end
        return results
    end

    -- Scans every file named by the location files.
    -- Returns results, stats where stats = { files = n, scanners = n, entries = n }.
    function self.scan_all()
        local files, err = self.files()
        if not files then return nil, err end

        local results = {}
        for _, path in ipairs(files) do
            local found = self.scan_file(path)   -- glob matched it, so unreadable -> skip
            if found then
                for _, r in ipairs(found) do results[#results + 1] = r end
            else
                warn("cannot read " .. path)
            end
        end
        return results, { files = #files, scanners = #scanners, entries = #results }
    end

    return self
end

return M

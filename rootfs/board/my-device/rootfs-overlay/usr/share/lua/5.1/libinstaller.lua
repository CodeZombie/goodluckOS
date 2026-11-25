-- libinstaller.lua
-- A headless library for managing Linux firmware applications

local installer = {}

local HOME = os.getenv("HOME") or "/home/player"
local APPS_DIR = HOME .. "/apps"

-- ============================================================================
-- Utility Functions
-- ============================================================================

-- Helper to safely escape shell arguments
local function shell_escape(s)
    if not s then return "''" end
    return "'" .. string.gsub(s, "'", "'\\''") .. "'"
end

-- Helper to check if a file/directory exists
local function exists(path)
    local f = io.open(path, "r")
    if f then
        f:close()
        return true
    end
    return false
end

-- Helper to read an entire file or command output
local function read_all(cmd_or_file, is_cmd)
    local f, err
    if is_cmd then
        f, err = io.popen(cmd_or_file, "r")
    else
        f, err = io.open(cmd_or_file, "r")
    end

    if not f then return nil, err end
    local content = f:read("*a")
    f:close()
    return content
end

-- Generates a safe folder name from the app name (replicating the C++ logic)
local function folder_from_name(name)
    local out = string.gsub(name, "[/\\:*?\"<>|%c]", "-")
    out = string.gsub(out, "^[%.%s]+", "") -- trim leading dots and spaces
    out = string.gsub(out, "[%.%s]+$", "") -- trim trailing dots and spaces
    return string.sub(out, 1, 128)
end

-- Executes a shell hook if it exists in the app folder
local function run_hook(app_dir, hook_name)
    local hook_path = app_dir .. "/" .. hook_name
    if exists(hook_path) then
        -- Run via `sh` so it executes even if it lost its +x permission in the zip
        local cmd = string.format("cd %s && sh ./%s", shell_escape(app_dir), shell_escape(hook_name))
        os.execute(cmd)
    end
end

-- ============================================================================
-- Core Library Functions
-- ============================================================================

-- Placeholder for your existing puppyfile parsing logic
function installer.parse_puppyfile(text)
    -- Your real library handles this. For demonstration, we'll return a mock table.
    -- Ensure it extracts at least { name = "...", icon = "..." }
    local metadata = {
        name = "Unknown App",
        icon = "icon.png"
    }
    for line in string.gmatch(text, "([^\r\n]+)") do
        local k, v = string.match(line, "^%s*([%w_]+)%s*=%s*(.-)%s*$")
        if k and v then
            metadata[string.lower(k)] = v
        end
    end
    return metadata
end

-- Inspects a zip, finds app.puppy, and returns metadata + the internal prefix path
function installer.get_metadata(zip_filepath)
    -- Find the exact path of app.puppy inside the zip using `unzip -l`
    --
    -- Search the zip for a .puppy$ file
    local cmd = string.format("unzip -l %s 2>/dev/null | grep '.puppy$' | awk '{print $NF}'", shell_escape(zip_filepath))
    local puppy_path = read_all(cmd, true)

    if not puppy_path or puppy_path == "" then
        return nil, "No .puppy file found in zip."
    end

    -- Extract prefix (e.g. "MyGame-1.0/" if the puppy file is inside a root folder)
    local prefix = string.match(puppy_path, "^(.*)app%.puppy$") or ""

    -- Read the contents of app.puppy directly from the zip
    local cat_cmd = string.format("unzip -p %s %s 2>/dev/null", shell_escape(zip_filepath), shell_escape(puppy_path))
    local puppy_text = read_all(cat_cmd, true)

    if not puppy_text or puppy_text == "" then
        return nil, "Failed to read app.puppy from zip."
    end

    local meta = installer.parse_puppyfile(puppy_text)
    meta._internal_prefix = prefix

    return meta
end

-- Extracts the icon from the zip to a specified output path
function installer.extract_icon(zip_filepath, out_path)
    local meta, err = installer.get_metadata(zip_filepath)
    if not meta then return false, err end
    if not meta.icon then return false, "No icon specified in app.puppy" end

    -- The icon path is relative to the puppy file, so we prepend the prefix
    local icon_internal_path = meta._internal_prefix .. meta.icon

    -- Extract just the icon file and dump its contents to out_path
    local cmd = string.format("unzip -p %s %s > %s 2>/dev/null",
        shell_escape(zip_filepath),
        shell_escape(icon_internal_path),
        shell_escape(out_path)
    )

    local success = os.execute(cmd)
    if not success then
        return false, "Failed to extract icon from zip."
    end
    return true
end

-- Installs an application from a zip file
function installer.install(zip_filepath)
    -- TODO: add alternate installation directory (ie: TF2)
    --
    local meta, err = installer.get_metadata(zip_filepath)
    if not meta then return false, err end
    local folder_name
    if meta.author ~= nil and meta.author ~= "" then
        folder_name = meta.author .. "." .. meta.name
    end
    folder_name = folder_from_name(folder_name)
    local target_dir = APPS_DIR .. "/" .. folder_name
    local tmp_dir = APPS_DIR .. "/.install-" .. folder_name
    local old_dir = APPS_DIR .. "/.old-" .. folder_name

    os.execute("mkdir -p " .. shell_escape(APPS_DIR))

    -- 2. Detect Update Status & Run Pre-Update Hook
    local is_update = exists(target_dir)
    if is_update then
        run_hook(target_dir, "pre-update.sh")
    end

    -- 3. Extract to Temporary Directory
    os.execute("rm -rf " .. shell_escape(tmp_dir))

    -- If there's no prefix, unpack directly into tmp_dir.
    -- If there is a prefix, unpack into tmp_dir and then lift the contents up one level.
    if meta._internal_prefix == "" then
        local unzip_cmd = string.format("unzip -q %s -d %s", shell_escape(zip_filepath), shell_escape(tmp_dir))
        if not os.execute(unzip_cmd) then
            os.execute("rm -rf " .. shell_escape(tmp_dir))
            return false, "Failed to unzip application."
        end
    else
        -- Use an intermediate extraction so we can strip the root folder
        local raw_tmp = tmp_dir .. "-raw"
        os.execute("rm -rf " .. shell_escape(raw_tmp))
        os.execute("mkdir -p " .. shell_escape(tmp_dir))

        local unzip_cmd = string.format("unzip -q %s -d %s", shell_escape(zip_filepath), shell_escape(raw_tmp))
        if not os.execute(unzip_cmd) then
            os.execute("rm -rf " .. shell_escape(raw_tmp))
            return false, "Failed to unzip application."
        end

        -- Move contents of the prefixed folder into the real tmp_dir
        local mv_cmd = string.format("mv %s/* %s/.[!.]* %s/ 2>/dev/null",
            shell_escape(raw_tmp .. "/" .. meta._internal_prefix),
            shell_escape(raw_tmp .. "/" .. meta._internal_prefix),
            shell_escape(tmp_dir)
        )
        os.execute(mv_cmd)
        os.execute("rm -rf " .. shell_escape(raw_tmp))
    end

    -- 4. Atomic Swap (Crash safety)
    os.execute("rm -rf " .. shell_escape(old_dir))
    if is_update then
        os.execute(string.format("mv %s %s", shell_escape(target_dir), shell_escape(old_dir)))
    end

    local swap_success = os.execute(string.format("mv %s %s", shell_escape(tmp_dir), shell_escape(target_dir)))
    if not swap_success then
        -- Swap failed, attempt rollback
        if is_update then
            os.execute(string.format("mv %s %s", shell_escape(old_dir), shell_escape(target_dir)))
        end
        os.execute("rm -rf " .. shell_escape(tmp_dir))
        return false, "Failed to move app into final destination."
    end

    -- 5. Run Post-Install or Post-Update Hooks
    if is_update then
        run_hook(target_dir, "post-update.sh")
    else
        run_hook(target_dir, "post-install.sh")
    end

    -- 6. Cleanup & OS Sync
    os.execute("rm -rf " .. shell_escape(old_dir))
    os.execute("rm -f " .. shell_escape(zip_filepath)) -- Delete the zip file upon success
    os.execute("sync")

    os.execute("app-cache scan " .. shell_escape(target_dir:gsub("/$", "")  .. "/app.puppy"))

    return true, "Successfully installed " .. meta.name
end

-- Uninstalls an application by folder name
function installer.uninstall(folder_name)
    local target_dir = APPS_DIR .. "/" .. folder_name

    if not exists(target_dir) then
        return false, "App is not installed."
    end

    -- 1. Run Pre-Uninstall Hook
    run_hook(target_dir, "pre-uninstall.sh")

    -- 2. Delete Folder
    os.execute("rm -rf " .. shell_escape(target_dir))

    -- 3. Cleanup Autolaunch
    local autolaunch_path = HOME .. "/autolaunch"
    if exists(autolaunch_path) then
        local al_data = read_all(autolaunch_path, false)
        if al_data and (string.find(al_data, target_dir .. "'", 1, true) or string.find(al_data, target_dir .. "/", 1, true)) then
            os.execute("rm -f " .. shell_escape(autolaunch_path))
        end
    end

    -- 4. Sync OS
    os.execute("sync")

    -- Verify deletion
    if exists(target_dir) then
        return false, "Failed to completely remove application directory."
    end

    -- TODO: Remove the entry from the app cache

    return true, "Successfully uninstalled " .. folder_name
end

-- Clean up orphans from interrupted installations (Call this on boot/script start)
function installer.recover()
    local p = io.popen(string.format("ls -1 %s 2>/dev/null", shell_escape(APPS_DIR)), "r")
    if not p then return end

    for dir in p:lines() do
        -- Check for .old- prefixes (an update was interrupted)
        local old_name = string.match(dir, "^%.old%-(.+)$")
        if old_name then
            local live_path = APPS_DIR .. "/" .. old_name
            local old_path = APPS_DIR .. "/" .. dir
            -- If the live path isn't there, the swap failed mid-way. Restore it.
            if not exists(live_path) then
                os.execute(string.format("mv %s %s", shell_escape(old_path), shell_escape(live_path)))
            else
                -- Otherwise, the new one succeeded, just delete the old one.
                os.execute("rm -rf " .. shell_escape(old_path))
            end
        end

        -- Check for .install- prefixes (an install was interrupted)
        local install_name = string.match(dir, "^%.install%-(.+)$")
        if install_name then
            os.execute("rm -rf " .. shell_escape(APPS_DIR .. "/" .. dir))
        end
    end
    p:close()
end

return installer

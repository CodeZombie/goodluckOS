-- puppy.lua : A .puppy file parser.
-- Puppy files are similar to INI, with a few differences:
-- Categories can be repeated. One .puppy file can contain zero or many [ENTRY], [ARCHIVE] or [LAUNCHER] entries.
--
local puppy = {}

local function trim(s)
    return s:match("^%s*(.-)%s*$")
end

local function split(str, sep)
    local res = {}
    local start = 1
    while true do
        local pos, end_pos = str:find(sep, start, true) -- true for plain text search
        if not pos then
            table.insert(res, trim(str:sub(start)))
            break
        end
        table.insert(res, trim(str:sub(start, pos - 1)))
        start = end_pos + 1
    end
    return res
end

-- Strips inline comments starting with `;`, but ignores them if they are inside double quotes
local function strip_inline_comment(line)
    local in_quotes = false
    local comment_pos = nil
    for i = 1, #line do
        local c = line:sub(i, i)
        if c == '"' then
            in_quotes = not in_quotes
        elseif c == ';' and not in_quotes then
            comment_pos = i
            break
        end
    end

    if comment_pos then
        return line:sub(1, comment_pos - 1)
    end
    return line
end

-- Main Parser Function
function puppy.parse(content, overrides)
    overrides = overrides or {}
    local result = {}

    -- Optimize override lookups: override_map[section][key] = condition_table
    local override_map = {}
    for _, cond in ipairs(overrides) do
        if cond.section and cond.key then
            local sec = cond.section
            if not override_map[sec] then override_map[sec] = {} end
            override_map[sec][cond.key] = cond
        end
    end

    local current_section_name = nil
    local current_section_ref = nil

    -- Iterate line by line
    for line in content:gmatch("[^\r\n]+") do
        local trimmed_line = trim(line)

        -- 1. Lines that start with "#" are comments
        if trimmed_line:sub(1, 1) ~= "#" and trimmed_line ~= "" then

            -- 2. Strip inline ';' comments (quote aware)
            line = strip_inline_comment(line)
            trimmed_line = trim(line)

            if trimmed_line ~= "" then
                -- Check for section headers: [SECTION_NAME]
                local sec = trimmed_line:match("^%[%s*(.-)%s*%]$")
                if sec then
                    current_section_name = sec
                    -- Initialize section list if it doesn't exist
                    if not result[sec] then
                        result[sec] = {}
                    end
                    -- Create a new table for this specific block (supports duplicates)
                    current_section_ref = {}
                    table.insert(result[sec], current_section_ref)
                else
                    -- Check for Key=Value pairs
                    local key, val = trimmed_line:match("^([^=]+)=(.*)$")
                    if key and val then
                        key = trim(key)
                        val = trim(val)

                        -- Strip surrounding quotes from the value if present
                        local unquoted = val:match('^"(.*)"$')
                        if unquoted then
                            val = unquoted
                        end

                        -- Apply special conditions/overrides
                        if current_section_name and override_map[current_section_name] and override_map[current_section_name][key] then
                            local cond = override_map[current_section_name][key]
                            if cond.type == "array" then
                                -- use 'seperator' (prompt spelling) or 'separator' (standard)
                                local sep = cond.separator or cond.seperator or ","
                                val = split(val, sep)
                            end
                        end

                        -- Assign to the current section block, or a global table if no section was defined yet
                        if current_section_ref then
                            current_section_ref[key] = val
                        else
                            if not result["_GLOBAL"] then result["_GLOBAL"] = {{}} end
                            result["_GLOBAL"][1][key] = val
                        end
                    end
                end
            end
        end
    end

    return result
end

return puppy

--[[ SimpQL - A declarative database abstraction library for SQLite3.

SimpQL is designed to make complex database creation, writing and merging
as simple and efficient as possible by offloading all the heavy lifting
directly into native SQLite triggers.

Instead of writing complex SQL queries by hand, you define your database
schema as a Lua table. This table defines your columns, their data types,
and their specific on-insert conflict resolution rules.

When you pass this schema to SimpQL, it automatically generates three things:
- Base Tables: The actual SQLite tables where your data lives.
- An Upsert View: A proxy view ("<table-name>_upsert") that sits in front
  of your base table.
- INSTEAD OF INSERT Triggers: These triggers are attached to the Upsert View
  and contains all of your custom conflict rules translated into native SQLite logic.

Now, when you call `upsert_row({...})`, SimpQL just passes a simple insert
statement to the Upsert View. The native SQLite trigger intercepts it,
checks the primary key, validates constraints, automatically casts Lua
tables into binary JSONB blobs, and routes the new data based on your rules
(keeping old values, overwriting them, or deep-merging JSON objects).
]]

local sqlite3 = require("lsqlite3")
local cjson = require("cjson")

local SimpQL = {}

SimpQL.TYPE = {
    TEXT = "TEXT",
    INTEGER = "INTEGER",
    REAL = "REAL",
    BLOB = "BLOB",
    JSONB = "JSONB",
    TIMESTAMP = "TIMESTAMP"
}

SimpQL.UPDATE_STRATEGY = {
    KEEP = "KEEP",                   -- Ignore the new value, keep the existing database value
    OVERWRITE = "OVERWRITE",         -- Overwrite the database value with the new value, if the new value is provided
    JSONB_MERGE = "JSONB_MERGE", -- Deep merge the new JSON into the existing JSON using jsonb_patch
    RESET_IF_MISSING = "RESET_IF_MISSING"  -- Reset to default (or NULL) if the column is missing (or NULL).
}

local DatabasePrototype = {}
DatabasePrototype.__index = DatabasePrototype

local function _get_default_fallback_string(column_type, column_default)
    local default_fallback = nil
    if column_type == SimpQL.TYPE.TIMESTAMP then
        return "unixepoch()"
    end
    if column_default ~= nil then
        local default_type = type(column_default)

        if default_type == "number" then
            default_fallback = tostring(column_default)

        elseif default_type == "boolean" then
            default_fallback = column_default and "1" or "0"

        elseif default_type == "string" then
            -- Escape any single quotes in the string for SQL by doubling them
            local escaped_string = column_default:gsub("'", "''")
        default_fallback = string.format("'%s'", escaped_string)

        elseif default_type == "table" then
            -- Serialize the table to a JSON string and escape it
            local json_string = cjson.encode(column_default)
            local escaped_json = json_string:gsub("'", "''")

            -- If it's a JSONB column, ensure we wrap the encoded table in jsonb()
            if column_type == SimpQL.TYPE.JSONB then
                default_fallback = string.format("jsonb('%s')", escaped_json)
            else
                default_fallback = string.format("'%s'", escaped_json)
            end
        end

    elseif column_type == SimpQL.TYPE.JSONB then
        -- Retain our hardcoded fallback for JSONB columns if no default is provided
        default_fallback = "jsonb('{}')"
    end
    return default_fallback
end

local function _enable_tracing_on_connection(connection)
    connection:trace(function(udata, sql)
        print("[SimpQL]:", sql)
    end)
end

function SimpQL.new()
    local database_instance = {
        connection = nil
    }
    setmetatable(database_instance, DatabasePrototype)
    return database_instance
end

-- With tracing on, sqlite3 wil print all succesfully executed statements to stdout.
function DatabasePrototype:enable_tracing()
    self.tracing = true
    if self.connection then
        _enable_tracing_on_connection(self.connection)
    end
end

function DatabasePrototype:open(file_path)
    self.connection = sqlite3.open(file_path)
    if not self.connection then
        return "Failed to open database at path: " .. tostring(file_path) .. ": " .. self.connection:errmsg()
    end

    if self.tracing then
        _enable_tracing_on_connection(self.connection)
    end

    if self.connection:exec("PRAGMA foreign_keys = ON;") ~= sqlite3.OK then
        return self.connection:errmsg()
    end
    if self.connection:exec("PRAGMA journal_mode = WAL;") ~= sqlite3.OK then
        return self.connection:errmsg()
    end

    return sqlite3.OK
end

function DatabasePrototype:close()
    if self.connection then
        self.connection:close()
        self.connection = nil
    end
    return sqlite3.OK
end

--- Retrieves the current user_version of the SQLite database
function DatabasePrototype:get_database_version()
    if not self.connection then
        return nil
    end

    local ok, version = pcall(function()
        local statement = self.connection:prepare("PRAGMA user_version;")
        if not statement then
            return nil
        end

        local val = nil
        if statement:step() == sqlite3.ROW then
            val = statement:get_value(0)
        end
        statement:finalize()
        return val
    end)

    return ok and version or nil
end

--- Sets the user_version of the SQLite database
function DatabasePrototype:set_database_version(new_version)
    if self.connection:exec(string.format("PRAGMA user_version = %d;", new_version)) ~= sqlite3.OK then
        return self.connection:errmsg()
    end
end

--- Applies the schema definitions, handling version checks and generating views/triggers
--- Returns nil on success, or an error message on failure.
function DatabasePrototype:apply_schema(schema_definition)
    if not schema_definition.version then
        return "Cannot apply database schema without a `version` field."
    end

    local current_version = self:get_database_version()

    if current_version > schema_definition.version then
        return string.format("Error: Database version (%d) is higher than schema version (%d).", current_version, schema_definition.version)
    end

    if current_version == schema_definition.version then
        return nil -- No schema update needed
    end

    -- Wrap the schema generation in a transaction for safety
    self.connection:exec("BEGIN TRANSACTION;")

    for _, table_definition in ipairs(schema_definition.tables) do
        local table_name = table_definition.name
        local identity_key = table_definition.identity_key

        -- Build the CREATE TABLE statement
        local column_declarations = {}
        for _, column in ipairs(table_definition.columns) do
            local declaration = string.format("%s %s %s", column.name, column.type, column.sql or "")
            table.insert(column_declarations, declaration)
        end
        local create_table_query = string.format(
            "CREATE TABLE IF NOT EXISTS %s (\n    %s\n);",
            table_name,
            table.concat(column_declarations, ",\n    ")
        )
        if self.connection:exec(create_table_query) ~= sqlite3.OK then
            return self.connection:errmsg()
        end
        -- Build the View for Upsertsl
        local view_name = table_name .. "_upsert"
        if self.connection:exec(string.format("DROP VIEW IF EXISTS %s;", view_name)) ~= sqlite3.OK then
            return self.connection:errmsg()
        end
        if self.connection:exec(string.format("CREATE VIEW %s AS SELECT * FROM %s;", view_name, table_name)) ~= sqlite3.OK then
            return self.connection:errmsg()
        end

        -- Build the "INSTEAD OF INSERT" trigger
        local insert_columns = {}
        local insert_values = {}
        local update_statements = {}

        for _, column in ipairs(table_definition.columns) do
            table.insert(insert_columns, column.name)

            -- if this is a JSONB column, convert the incoming json-text-string to a JSONB blob
            local new_val_expr = string.format("NEW.%s", column.name)
            if column.type == SimpQL.TYPE.JSONB then
                new_val_expr = string.format("jsonb(NEW.%s)", column.name)
            end

            -- We must satisfy NOT NULL constraints during the INSERT evaluation phase before ON CONFLICT triggers.
            local default = _get_default_fallback_string(column.type, column.default)
            local csv_default = ""
            if default then
                csv_default = string.format(", %s", default)
            end

            if column.name == identity_key then
                -- If a default is provided for the primary key, coalesce to it. Otherwise, pass the raw value.
                if default then
                    table.insert(insert_values, string.format("coalesce(%s, %s)", new_val_expr, default))
                else
                    table.insert(insert_values, new_val_expr)
                end
            else
                table.insert(insert_values, string.format("coalesce(%s, (SELECT %s FROM %s WHERE %s = NEW.%s)%s)",
                    new_val_expr, column.name, table_name, identity_key, identity_key, csv_default))
            end

            -- Build the ON CONFLICT DO UPDATE SET clauses
            if column.name ~= identity_key then
                if column.on_update == SimpQL.UPDATE_STRATEGY.KEEP then
                    table.insert(update_statements, string.format("%s = %s.%s", column.name, table_name, column.name))
                elseif column.on_update == SimpQL.UPDATE_STRATEGY.JSONB_MERGE then
                    table.insert(update_statements, string.format("%s = jsonb_patch(%s.%s, coalesce(%s, jsonb('{}')))",
                        column.name, table_name, column.name, new_val_expr))
                elseif column.on_update == SimpQL.UPDATE_STRATEGY.RESET_IF_MISSING then
                    -- If the new value is missing, reset it to the  default (or NULL)
                    table.insert(update_statements, string.format("%s = coalesce(%s, %s)", column.name, new_val_expr, default or "NULL"))
                else
                    table.insert(update_statements, string.format("%s = coalesce(%s, %s.%s)", column.name, new_val_expr, table_name, column.name))
                end
            end
        end

        local trigger_name = table_name .. "_upsert_trigger"
        if self.connection:exec(string.format("DROP TRIGGER IF EXISTS %s;", trigger_name)) ~= sqlite3.OK then
            return self.connection:errmsg()
        end

        local create_trigger_query = string.format([[
            CREATE TRIGGER %s
            INSTEAD OF INSERT ON %s
            FOR EACH ROW
            BEGIN
                INSERT INTO %s (%s)
                VALUES (%s)
                ON CONFLICT(%s) DO UPDATE SET
                    %s;
            END;
        ]],
        trigger_name,
        view_name,
        table_name,
        table.concat(insert_columns, ", "),
        table.concat(insert_values, ", "),
        identity_key,
        table.concat(update_statements, ",\n                    "))
        if self.connection:exec(create_trigger_query) ~= sqlite3.OK then
            return self.connection:errmsg()
        end
    end

    self:set_database_version(schema_definition.version)
    if self.connection:exec("COMMIT;") ~= sqlite3.OK then
        return self.connection:errmsg()
    end

    return nil -- No errors
end

function DatabasePrototype:upsert_row(table_name, row_data)
    local column_names = {}
    local parameter_placeholders = {}
    local bound_values = {}

    for column_name, value in pairs(row_data) do
        table.insert(column_names, column_name)
        table.insert(parameter_placeholders, "?")

        -- Serialize lua tables to JSON strings automatically
        if type(value) == "table" then
            table.insert(bound_values, cjson.encode(value))
        else
            table.insert(bound_values, value)
        end
    end

    local view_name = table_name .. "_upsert"
    local insert_query = string.format(
        "INSERT INTO %s (%s) VALUES (%s);",
        view_name,
        table.concat(column_names, ", "),
        table.concat(parameter_placeholders, ", ")
    )

    local prepared_statement = self.connection:prepare(insert_query)
    if not prepared_statement then
        return "Failed to prepare statement: " .. self.connection:errmsg()
    end

    if prepared_statement:bind_values(unpack(bound_values)) ~= sqlite3.OK then
        return "Failed to bind values: " .. self.connection:errmsg()
    end

    local result_code = prepared_statement:step()
    if prepared_statement:finalize() ~= sqlite3.OK then
        return "Failed to finalize prepared statement: " .. self.connection:errmsg()
    end

    if result_code ~= sqlite3.DONE then
        return "Failed to execute upsert: " .. self.connection:errmsg()
    end

    return nil
end

SimpQL.sqlite3 = sqlite3

return SimpQL

-- libappcache.lua
-- A library for creating and interacting with the app cache database file.
--
-- TODO: add some common functions:
--      - get current launcher
--      - set launcher
--      - set app to autolaunch
--      - upsert_rows() -- note: takes multiple entries and inserts them in bulk - much faster than doing one upsert_row-per-app

local SimpQL = require("simpql")
local cjson = require("cjson")
local stat = require("posix.sys.stat")

local M = {}

M.DB_PATH  = "/var/lib/goodluckos/apps.sqlite"

M.DATABASE_VERSION = 1 -- if you make ANY changes to the schema (the tables defined below), you MUST increment this value.

local apps_table = {
    name = "apps",
    identity_key = "command",
    columns = {
        {
            name = "id",
            type = SimpQL.TYPE.INTEGER,
            sql = "PRIMARY KEY",
        },
        {
            name = "command",
            type = SimpQL.TYPE.TEXT,
            sql = "NOT NULL UNIQUE",
            on_update = SimpQL.UPDATE_STRATEGY.KEEP,
        },
        {
            -- The path to this app's file or directory.
            -- The correct value for this field is whatever file/dir that, if deleted, would remove the app from the system.
            name = "path",
            type = SimpQL.TYPE.TEXT,
            on_update = SimpQL.UPDATE_STRATEGY.KEEP,
        },
        {
            name = "name",
            type = SimpQL.TYPE.TEXT,
            sql = "NOT NULL",
            on_update = SimpQL.UPDATE_STRATEGY.OVERWRITE
        },
        {
            name = "category",
            type = SimpQL.TYPE.TEXT,
            on_update = SimpQL.UPDATE_STRATEGY.OVERWRITE
        },
        {
            name = "description",
            type = SimpQL.TYPE.TEXT,
            on_update = SimpQL.UPDATE_STRATEGY.OVERWRITE,
        },
        {
            name = "icon_filepath",
            type = SimpQL.TYPE.TEXT,
            on_update = SimpQL.UPDATE_STRATEGY.OVERWRITE,
        },
        {
            name = "launcher",
            type = SimpQL.TYPE.INTEGER,
            sql = "NOT NULL CHECK (launcher IN (0, 1))",
            default = 0,
            on_update = SimpQL.UPDATE_STRATEGY.OVERWRITE,
        },
        {
            name = "terminal",
            type = SimpQL.TYPE.INTEGER,
            sql = "NOT NULL CHECK (terminal IN (0, 1))",
            default = 0,
            on_update = SimpQL.UPDATE_STRATEGY.OVERWRITE,
        },
        {
            name = "last_scan_id",
            sql = "INTEGER NOT NULL",
            default = 0,
            on_update = SimpQL.UPDATE_STRATEGY.OVERWRITE,
        },
        {
            name = "metadata",
            type = SimpQL.TYPE.JSONB,
            sql = "NOT NULL",
            on_update = SimpQL.UPDATE_STRATEGY.JSONB_MERGE,
        }
    }
}

local active_launcher_table  = {
    name = "active_launcher",
    identity_key = "id",
    columns = {
        {
            name = "id",
            type = SimpQL.TYPE.INTEGER,
            sql = "PRIMARY KEY CHECK (id = 1)",
            default=1,
        },
        {
            name = "app_id",
            type = SimpQL.TYPE.INTEGER,
            sql = "REFERENCES apps(id) ON DELETE SET NULL"
        }
    }
}

local autolaunch_table  = {
    name = "autolaunch",
    identity_key = "id",
    columns = {
        {
            name = "id",
            type = SimpQL.TYPE.INTEGER,
            default=1,
            sql = "PRIMARY KEY CHECK (id = 1)"
        },
        {
            name = "app_id",
            type = SimpQL.TYPE.INTEGER,
            sql = "REFERENCES apps(id) ON DELETE SET NULL"
        }
    }
}

function M.open()
    local db = SimpQL.new()
    local rc = db:open(M.DB_PATH)
    if rc ~= SimpQL.sqlite3.OK then return nil, rc end

    if db:get_database_version() ~= M.DATABASE_VERSION then
        rc = db:apply_schema({
            version = M.DATABASE_VERSION,
            tables = {
                apps_table,
                active_launcher_table,
                autolaunch_table
            }
        })
        if rc then return nil, rc end
    end

    stat.chmod(M.DB_PATH, tonumber("666", 8))

    return db, nil
end

function M.get_app_categories()
    local db, err = M.open()
    if err then return nil, err end
    categories = {}
    for row in db.connection:nrows("SELECT DISTINCT category FROM apps WHERE category IS NOT NULL") do
        table.insert(categories, row.category)
    end
    db.connection:close()
    return categories
end

function M.get_apps_in_category(category)
    local db, err = M.open()
    if err then return nil, err end
    apps = {}
    for app in db.connection:nrows(string.format("SELECT *, json(metadata) AS metadata_json FROM apps WHERE category = \"%s;", category)) do
        if app.metadata_json then
            app.metadata = cjson.decode(app.metadata_json)
        else
            app.metadata = {}
        end
        app.metadata_json = nil
        table.insert(apps, app)
    end
    db.connection:close()
    return apps
end

return M
--
--
-- local apps_table_OLD = {
--     table   = "apps",
--     version = 1,
--     key     = "command",
--     columns = {
--         { name = "id",          sql = "INTEGER PRIMARY KEY", insert = false }, -- A number uniquely identifying this app. Not stable: deleting the sqlite file and rescanning will not necessarily assign the same IDs as before.
--         { name = "command",     sql = "TEXT NOT NULL UNIQUE", update = "keep" }, -- The terminal command which launches the application. Unique and stable.
--         { name = "name",        sql = "TEXT NOT NULL", utf8_clean = true, update = "overwrite" }, -- The user-facing name of the application.
--         { name = "location",    sql = "TEXT", update = "coalesce" }, -- Path to the data file/folder. For a rom, the rom file; for an app, its data folder (e.g. /home/player/apps/mediabox). A real path, so no utf8_clean.
--         { name = "description", sql = "TEXT", utf8_clean = true, update = "coalesce" }, -- A user-facing description (optional).
--         { name = "category",    sql = "TEXT", utf8_clean = true, update = "coalesce" }, -- The category this application belongs to (optional).
--         { name = "icon",        sql = "TEXT", update = "coalesce" }, -- Path to this application's icon image.
--         { name = "source",      sql = "TEXT", update = "overwrite" }, -- File the entry was scanned from (e.g. /home/player/apps/Mediabox/app.puppy).
--         { name = "scanner",     sql = "TEXT", update = "overwrite" }, -- Scanner that produced it (e.g. puppy.lua).
--         { name = "last_seen",   sql = "INTEGER NOT NULL DEFAULT 0", update = "overwrite" }, -- Scan id (unix time) of last sighting; drives --prune.
--         { name = "autolaunch",  sql = "INTEGER NOT NULL DEFAULT 0 CHECK (autolaunch IN (0, 1))", bool_coercion = true, merge = "or", update = "keep" }, -- Whether this app autolaunches on boot. Owned by `app-cache autolaunch` (and appctl), so rescans never touch it ("keep").
--         { name = "terminal",    sql = "INTEGER NOT NULL DEFAULT 0", bool_coercion = true, merge = "or", update = "overwrite" }, -- Whether this is a terminal application.
--         { name = "metadata",    sql = "BLOB NOT NULL DEFAULT (jsonb('{}'))", json = true, update = "patch" }, -- Arbitrary metadata. Rescans merge keys, so keys added by other tools survive.
--     },
--     indexes = {
--         idx_apps_name          = "name COLLATE NOCASE",
--         idx_apps_category_name = "category, name COLLATE NOCASE",
--     },
-- }
--
-- local launchers_table = {
--     table = "launchers",
--     version = 1,
--     key = "command",
--     columns = {
--         { name = "id", sql = "INTEGER PRIMARY KEY", insert = false },
--         { name = "command", sql = "TEXT NOT NULL UNIQUE", update = "keep" },
--         { name = "name", sql = "TEXT NOT NULL", utf8_clean = true, update = "overwrite" },
--         { name = "location", sql = "TEXT", update = "coalesce" },
--         { name = "description", sql = "TEXT", utf8_clean = true,  update = "coalesce" },
--         { name = "source", sql = "TEXT", update = "overwrite" },
--         { name = "scanner", sql = "TEXT", update = "overwrite" },
--         { name = "active", sql = "INTEGER NOT NULL DEFAULT 0 CHECK (active IN (0, 1))", update = "keep" },
--         { name = "metadata", sql = "BLOB NOT NULL DEFAULT (jsonb('{}'))", json = true, update = "patch" },
--     },
--     indexes = {
--         idx_apps_name = "name COLLATE NOCASE"
--     },
-- }
--
-- function M.open_db()
--     local db, err = seekwell.open(M.DB_PATH, { apps_table, launchers_table }, {
--         mkdir = true,
--         mode  = tonumber("666", 8), -- 0666 is world-writable, so anything can r/w the database
--     })
--     if not db then return nil, err end
--     return db, nil
-- end
--
-- return M

--[[============================================================================
@file    imexcfg.lua

@author  Daniel Zorychta

@brief   The IMport EXport ConFig functionallity

@note    Copyright (C) 2015 Daniel Zorychta <daniel.zorychta@gmail.com>

         This program is free software; you can redistribute it and/or modify
         it under the terms of the GNU General Public License as published by
         the  Free Software  Foundation;  either version 2 of the License, or
         any later version.

         This  program  is  distributed  in the hope that  it will be useful,
         but  WITHOUT  ANY  WARRANTY;  without  even  the implied warranty of
         MERCHANTABILITY  or  FITNESS  FOR  A  PARTICULAR  PURPOSE.  See  the
         GNU General Public License for more details.

         You  should  have received a copy  of the GNU General Public License
         along  with  this  program;  if not,  write  to  the  Free  Software
         Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.


==============================================================================]]

require("utils")
local lfs = require("lfs")

--==============================================================================
-- GLOBAL OBJECTS
--==============================================================================

--==============================================================================
-- LOCAL OBJECTS
--==============================================================================

--==============================================================================
-- LOCAL FUNCTIONS
--==============================================================================
--------------------------------------------------------------------------------
-- @brief  Function try found specified key in the selected line
-- @param  line         line where key is finding
-- @return flag name, value
--------------------------------------------------------------------------------
local function GetLineFlag(line)
    if line:match("^%s*#%s*define%s+__.*__%s+.*$") then
        return line:match("^%s*#%s*define%s+(__.*__)%s+(.*)$")
    elseif line:match("^%s*__.*__%s*=%s*.*$") then
        return line:match("^%s*(__.*__)%s*=%s*(.*)$")
    else
        return nil, nil
    end
end

--------------------------------------------------------------------------------
-- @brief  Load table from file
-- @param  file         file to read from
-- @param  Pulse        function for update progress dialog
-- @return On success table is returned, otherwise nil
--------------------------------------------------------------------------------
function CollectSettingsFromFile(tab, fname, path)
    tab[fname] = {}

    local flagOld = ""
    local valOld = ""
    local file = io.open(path, "rb")

    if file == nil then
        return nil
    end

    for line in file:lines() do
        local flag, val = GetLineFlag(line)

        if flag ~= nil and val ~= nil then
            if flag ~= flagOld then
                tab[fname][flag] = val
                flagOld = flag
                valOld = val
            elseif val ~= valOld then
                print(
                    "CollectSettings(): "
                        .. fname
                        .. " duplicated flag: '"
                        .. flag
                        .. "' with different values: '"
                        .. valOld
                        .. "', '"
                        .. val
                        .. "'"
                )
            end
        end
    end

    if file ~= nil then
        file:close()
    end
end

--------------------------------------------------------------------------------
-- @brief  Get all files from seleced path.
-- @param  path     root path
-- @praam  filter   file filter
-- @return List of files.
--------------------------------------------------------------------------------
function GetAllFiles(path, filter)
    local files = {}

    local function scan(dir)
        for file in lfs.dir(dir) do
            if file ~= "." and file ~= ".." then
                local f = dir .. "/" .. file
                local attr = lfs.attributes(f)

                if attr.mode == "directory" then
                    --print("DIR: " .. f)
                    scan(f)
                elseif file:match(filter) then
                    --print("FIL: " .. f)
                    table.insert(files, f)
                end
            end
        end
    end

    scan(path)
    return files
end

--==============================================================================
-- PUBLIC FUNCTIONS
--==============================================================================
--------------------------------------------------------------------------------
-- @brief  Load table from file
-- @param  file         file to read from
-- @return On success table is returned, otherwise nil
--------------------------------------------------------------------------------
function CollectSettings(tab, path)
    local files = GetAllFiles(path, "%.h$")
    for _, name in ipairs(files) do
        name = Dos2UnixFilename(name)

        local cwd_escaped = GetConfigPath():gsub("([^%w])", "%%%1")
        local fname = name:gsub(cwd_escaped, "")

        CollectSettingsFromFile(tab, fname, name)
    end
end

--------------------------------------------------------------------------------
-- @brief  Apply configuration to selected file
-- @param  setup        configuration table
-- @return true on success, otherwise false
--------------------------------------------------------------------------------
function ApplySettings(setup)
    local errors = ""

    for file, keys in pairs(setup) do
        if KeyWrite(file, keys) == false then
            print("You have not permissions to write project configuration or file '" .. file .. "' not exists.")
            errors = errors .. " - file not exists: " .. file .. "\n"
        end
    end

    return true
end

--------------------------------------------------------------------------------
-- @brief  Save configuration to selected file
-- @param  path         file to export
-- @return None
--------------------------------------------------------------------------------
function ExportConfiguration(path, parser)
    local setup = {}
    CollectSettings(setup, GetConfigPath())
    SaveTable(setup, path)
end

--------------------------------------------------------------------------------
-- @brief  Load configuration from selected file
-- @param  path         file to export
-- @return None
--------------------------------------------------------------------------------
function ImportConfiguration(path, parser)
    local setup = LoadTable(path)

    if setup == nil then
        print("Incorrect configuration")
        os.exit(1)
    else
        if parser ~= nil then
            parser:CallEvent("ConfigImport")
        end

        ApplySettings(setup)
    end
end

--[[============================================================================
@file    configtool.lua

@author  Daniel Zorychta

@brief   This file is the main file of the configuration tool

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

os.setlocale("C")
-- tool directory: environment variable (set by configtool-cli script) or script
-- location (systems without environment variables, e.g. dnx RTOS)
local tool_dir = os.getenv("CONFIGTOOL_DIR") or (arg and arg[0] and arg[0]:match("^(.*)/[^/]*$")) or "."
package.path = tool_dir .. "/?.lua;" .. package.path

--==============================================================================
-- EXTERNAL MODULES
--==============================================================================
require("parser")
require("utils")
require("imexcfg")
local json = require("json")

--==============================================================================
-- PUBLIC OBJECTS
--==============================================================================

--==============================================================================
-- LOCAL OBJECTS
--==============================================================================

--==============================================================================
-- LOCAL FUNCTIONS
--==============================================================================
--------------------------------------------------------------------------------
-- @brief  Find main configuration file in arguments (argument without option).
-- @param  None
-- @return Config directory and root file name (relative to config directory)
--         or nil if not given.
--------------------------------------------------------------------------------
local function FindRootConfigFile()
    for i = 1, #arg do
        if not arg[i]:match("^%a+%=") and not arg[i]:match("help") then
            local path = Dos2UnixFilename(arg[i])
            return dirname(path) or "", basename(path) or path
        end
    end

    return nil, nil
end

--------------------------------------------------------------------------------
-- @brief  Convert file path given in command line to path relative to config
--         directory. Path relative to the current directory or to the config
--         directory is accepted.
-- @param  file         file path
-- @param  configDir    configuration directory
-- @return File path relative to config directory.
--------------------------------------------------------------------------------
local function ToConfigRelativePath(file, configDir)
    file = Dos2UnixFilename(file)

    local ndir = NormalizePath(configDir)
    if ndir ~= "" then
        local nfile = NormalizePath(file)
        if nfile:sub(1, #ndir + 1) == ndir .. "/" then
            return nfile:sub(#ndir + 2)
        end
    end

    return file
end

--------------------------------------------------------------------------------
-- @brief  Function create widgets
-- @param  None
-- @return None
--------------------------------------------------------------------------------
local function CheckArguments()
    local assignments = {}

    for i = 1, #arg do
        if arg[i]:match("help") then
            print("Usage: configtool [options] <config-file>")
            print("  getvalue=<file>:<key>          get value of <key> from <file>")
            print("  setvalue=<file>:<key>=<value>  set <value> of <key> in <file>; scripts of the")
            print("                                 file are executed as in the TUI (validation,")
            print("                                 'clicked' events, PreSave/PostSave). Option can")
            print("                                 be used many times. If <config-file> is given,")
            print("                                 menus from it to <file> are executed first.")
            print("  tolua=<file>                   read selected file and print as Lua table")
            print("  tojson=<file>                  read selected file and print as JSON")
            print("  import=<file>:<workdir>        import selected configuration from <file> to <workdir>")
            print("  export=<file>:<workdir>,<confdir>,[confdir..]  export <confdir> located in <workdir> to <file>")
            os.exit(1)
        elseif arg[i]:match("import%=.*") then
            local arg = arg[i]:match("import%=(.*)")
            local path, cwd = arg:match("^(.*):(.*)$")
            path = Dos2UnixFilename(path)
            cwd = Dos2UnixFilename(cwd)

            SetConfigPath(cwd .. "/")
            ImportConfiguration(path, CreateParser())
            print("Configuration imported.")
            os.exit(0)
        elseif arg[i]:match("export%=.*") then
            local arg = arg[i]:match("export%=(.*)")
            local path, cwd = arg:match("^(.*):(.*)$")
            path = Dos2UnixFilename(path)
            cwd = Dos2UnixFilename(cwd)

            local config = {}
            local paths = cwd:split(",")

            SetConfigPath(paths[1])

            if #paths == 1 then
                CollectSettings(config)
            else
                for i = 2, #paths do
                    CollectSettings(config, paths[1] .. paths[i])
                end
            end

            SaveTable(config, path)

            print("Configuration exported.")
            os.exit(0)
        elseif arg[i]:match("tolua%=.*") then
            local file = Dos2UnixFilename(arg[i]:match("tolua%=(.*)"))

            local config = {}
            CollectSettingsFromFile(config, file, file)
            print(SerializeTable(config[file], false))
            os.exit(0)
        elseif arg[i]:match("tojson%=.*") then
            local file = Dos2UnixFilename(arg[i]:match("tojson%=(.*)"))

            local config = {}
            CollectSettingsFromFile(config, file, file)
            print(json.encode(config[file]))
            os.exit(0)
        elseif arg[i]:match("getvalue%=.*") then
            local arg = arg[i]:match("getvalue%=(.*)")
            local file, key = arg:match("^(.*):(.*)$")
            file = Dos2UnixFilename(file)

            local config = {}
            CollectSettingsFromFile(config, file, file)
            print(config[file][key])
            os.exit(0)
        elseif arg[i]:match("setvalue%=.*") then
            local arg = arg[i]:match("setvalue%=(.*)")
            local file, key, value = arg:match("^(.*):([^:=]+)=(.*)$")
            if file == nil then
                io.stderr:write("[Error] incorrect format, use: setvalue=<file>:<key>=<value>\n")
                os.exit(1)
            end

            table.insert(assignments, { file = file, key = key, value = value })
        else
            SetConfigPath(dirname(arg[i]) or "")
        end
    end

    -- values are set after all arguments are read (config file can be given
    -- as the last argument)
    if #assignments > 0 then
        local configDir, rootFile = FindRootConfigFile()
        SetConfigPath(configDir or "")

        for _, a in ipairs(assignments) do
            a.file = ToConfigRelativePath(a.file, GetConfigPath())
        end

        os.exit(SetValuesFromCommandLine(rootFile, assignments))
    end
end

--------------------------------------------------------------------------------
-- @brief  Function create widgets
-- @param  None
-- @return None
--------------------------------------------------------------------------------
local function main()
    CheckArguments()

    local Parser = CreateParser()
    if type(arg[1]) == "string" then
        Parser:Parse(basename(arg[1]))
    end
end

main()

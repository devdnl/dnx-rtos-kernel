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
local tool_dir = os.getenv("CONFIGTOOL_DIR")
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
-- @brief  Function create widgets
-- @param  None
-- @return None
--------------------------------------------------------------------------------
local function CheckArguments()
    for i = 1, #arg do
        if arg[i]:match("help") then
            print("Usage: configtool [options] <config-file>")
            print("  getvalue=<file>:<key>          get value of <key> from <file>")
            print("  setvalue=<file>:<key>=<value>  set <value> of <key> in <file>")
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
        elseif arg[1]:match("tolua%=.*") then
            local file = Dos2UnixFilename(arg[i]:match("tolua%=(.*)"))

            local config = {}
            CollectSettingsFromFile(config, file, file)
            print(SerializeTable(config[file], false))
            os.exit(0)
        elseif arg[1]:match("tojson%=.*") then
            local file = Dos2UnixFilename(arg[i]:match("tojson%=(.*)"))

            local config = {}
            CollectSettingsFromFile(config, file, file)
            print(json.encode(config[file]))
            os.exit(0)
        elseif arg[1]:match("getvalue%=.*") then
            local arg = arg[i]:match("getvalue%=(.*)")
            local file, key = arg:match("^(.*):(.*)$")
            file = Dos2UnixFilename(file)

            local config = {}
            CollectSettingsFromFile(config, file, file)
            print(config[file][key])
            os.exit(0)
        elseif arg[1]:match("setvalue%=.*") then
            local arg = arg[i]:match("setvalue%=(.*)")
            local file, key, value = arg:match("^(.*):(.*)=(.*)$")
            file = Dos2UnixFilename(file)

            local config = {}
            CollectSettingsFromFile(config, file, file)
            config[file][key] = value
            ApplySettings(config)
            os.exit(0)
        else
            SetConfigPath(dirname(arg[i]))
        end
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

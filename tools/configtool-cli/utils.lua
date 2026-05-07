--[[============================================================================
@file    utils.lua

@author  Daniel Zorychta

@brief   The utility functions

@note    Copyright (C) 2014 Daniel Zorychta <daniel.zorychta@gmail.com>

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

--==============================================================================
-- GLOBAL OBJECTS
--==============================================================================

--==============================================================================
-- LOCAL OBJECTS
--==============================================================================
local cwd = ""

--==============================================================================
-- LOCAL FUNCTIONS
--==============================================================================
--------------------------------------------------------------------------------
-- @brief  Function try found specified key in the selected line
-- @param  line         line where key is finding
-- @param  key          key to find
-- @return true if key was found, otherwise false
--------------------------------------------------------------------------------
local function LineMatch(line, key)
    if line:match("^%s*#%s*define%s+" .. key .. "%s*.*") then
        return line:match("^%s*#%s*define%s+" .. key .. "%s*.*")
    elseif line:match("^%s*" .. key .. "%s*=%s*.*") then
        return line:match("^%s*" .. key .. "%s*=%s*.*")
    else
        return nil
    end
end

--------------------------------------------------------------------------------
-- @brief  Function modify selected line by adding new value to the specified key
-- @param  line         line to modify
-- @param  key          key to find
-- @param  value        value to modify
-- @return Modified line
--------------------------------------------------------------------------------
local function LineModify(line, key, value)
    if line:match("^%s*#%s*define%s+" .. key .. "%s*.*") then
        return "#define " .. key .. " " .. value
    elseif line:match("^%s*" .. key .. "%s*=%s*.*") then
        return key .. "=" .. value
    else
        return line
    end
end

--------------------------------------------------------------------------------
-- @brief  Function returns a value of selected key
-- @param  line         line from value is getting
-- @param  key          key from value is getting
-- @return Value of selected key
--------------------------------------------------------------------------------
local function LineGetKeyValue(line, key)
    if line:match("^%s*#%s*define%s+" .. key .. "%s*.*") then
        local _, _, value = line:find("%s*#%s*define%s+" .. key .. "%s+(.*)")
        return value
    elseif line:match("^%s*" .. key .. "%s*=%s*.*") then
        local _, _, value = line:find("%s*" .. key .. "%s*=%s*(.*)")
        return value
    else
        return ""
    end
end

--------------------------------------------------------------------------------
-- @brief  Returns a key and a value from selected line (only keys that are
--         defined to write/read: #define __FLAG_XX__ YY or __FLAG_XX__=YY)
-- @param  line         line from value is getting
-- @return On success return key, value. On error nil, nil is returned
--------------------------------------------------------------------------------
local function LineGetKeyAndValue(line)
    if line:match("^%s*#%s*define%s+(.*)%s+(.*)") then
        local _, _, k, v = line:find("^%s*#%s*define%s+(.*)%s+(.*)")
        return k, v
    elseif line:match("^%s*(.*)%s*=%s*(.*)") then
        local _, _, k, v = line:find("^%s*(.*)%s*=%s*(.*)")
        return k, v
    else
        return nil, nil
    end
end

--==============================================================================
-- PUBLIC FUNCTIONS
--==============================================================================
--------------------------------------------------------------------------------
-- @brief  Convert DOS path to unix.
-- @param  path     path
-- @return Unix path.
--------------------------------------------------------------------------------
function Dos2UnixFilename(path)
    path = path:gsub("\\", "/")
    path = path:gsub("//+", "/")
    return path
end

--------------------------------------------------------------------------------
-- @brief  Short IF
-- @param  condition    condition
-- @param  resultTrue   result if condition is true
-- @param  resultFalse  result if condition is false
-- @return result
--------------------------------------------------------------------------------
function iff(condition, resultTrue, resultFalse)
    if condition == true then
        return resultTrue
    else
        return resultFalse
    end
end

--------------------------------------------------------------------------------
-- @brief  Set current working directory
-- @param  condition    condition
-- @param  resultTrue   result if condition is true
-- @param  resultFalse  result if condition is false
-- @return result
--------------------------------------------------------------------------------
function SetConfigPath(path)
    cwd = Dos2UnixFilename(path)
end

--------------------------------------------------------------------------------
-- @brief  Get current working directory
-- @param  condition    condition
-- @param  resultTrue   result if condition is true
-- @param  resultFalse  result if condition is false
-- @return result
--------------------------------------------------------------------------------
function GetConfigPath()
    return cwd
end

--------------------------------------------------------------------------------
-- @brief  Function shows error dialog. Function kills entire wizard.
-- @param  title        window title
-- @param  caption      window caption
-- @param  parent       parent window
-- @param  doexit       exit from program if true
-- @return None
--------------------------------------------------------------------------------
function ShowErrorMessage(title, caption, doexit)
    print(title, caption)

    if doexit ~= false then
        os.exit(0)
    end
end

--------------------------------------------------------------------------------
-- @brief  Function shows info dialog
-- @param  title        window title
-- @param  caption      window caption
-- @param  parent       parent window
-- @return None
--------------------------------------------------------------------------------
function ShowInfoMessage(title, caption)
    print(title, caption)
end

--------------------------------------------------------------------------------
-- @brief  Function shows question dialog
-- @param  title        window title
-- @param  caption      window caption
-- @param  buttons      wxWidgets button definitions to show
-- @param  parent       parent window
-- @return Selected button
--------------------------------------------------------------------------------
function ShowQuestionMessage(title, caption, buttons)
    print(title, caption)
end

--------------------------------------------------------------------------------
-- @brief  Writes value of selected key
-- @param  filename     file name
-- @param  key          key
-- @param  value        value to write
-- @return On success true is returned. On error false is returned.
--------------------------------------------------------------------------------
function KeyWrite(filename, key, value)
    assert(type(filename) == "string", "KeyWrite(): Invalid type of 'filename' <" .. type(filename) .. ">\n")

    if type(key) == "table" then
        -- table is expected
    elseif type(key) == "string" then
        assert(type(value) == "string", "KeyWrite(): Invalid type of 'value' <" .. type(value) .. ">\n")
    else
        assert(
            false,
            "KeyWrite(): Invalid type of 'key' <" .. type(key) .. ">\n" .. "'key' shall be the table or string."
        )
    end

    -- read and modify file
    local content = {}
    local modLines = 0

    local file = io.open(cwd .. filename, "rb")
    if file ~= nil then
        for line in file:lines() do
            if type(key) == "table" then
                local match = false
                local k, v = LineGetKeyAndValue(line)
                if k ~= nil and v ~= nil then
                    for kk, kv in pairs(key) do
                        if LineMatch(line, kk) then
                            modLines = modLines + 1
                            content[#content + 1] = LineModify(line, kk, kv)
                            match = true
                            break
                        end
                    end
                end

                if match == false then
                    content[#content + 1] = line
                end
            else
                if LineMatch(line, key) then
                    modLines = modLines + 1
                    content[#content + 1] = LineModify(line, key, value)
                else
                    content[#content + 1] = line
                end
            end
        end

        file:close()
    else
        print("KeyWrite(): file not exists: '" .. filename .. "'")
        return false
    end

    -- write the file
    if modLines > 0 then
        file = io.open(cwd .. filename, "wb")
        if file ~= nil then
            for i, line in ipairs(content) do
                file:write(line, "\n")
            end
            file:close()
        else
            print("KeyWrite(): file write protected: '" .. filename .. "'")
            return false
        end
    end

    return true
end

--------------------------------------------------------------------------------
-- @brief  Reads value of selected key
-- @param  filename     file name
-- @param  key          key
-- @return On success a value with form of string, otherwise nil.
--------------------------------------------------------------------------------
function KeyRead(filename, key)
    assert(type(filename) == "string", "KeyRead(): Invalid type of 'filename' <" .. type(filename) .. ">\n")
    assert(type(key) == "string", "KeyRead(): Invalid type of 'key' <" .. type(key) .. ">\n")

    local file = io.open(cwd .. filename, "rb")
    assert(file, "KeyRead(): file does not exists: '" .. filename .. "'")

    local value = nil
    local mod_cnt = 0
    for line in file:lines() do
        if LineMatch(line, key) then
            mod_cnt = mod_cnt + 1
            value = LineGetKeyValue(line, key)
            if type(value) == "string" then
                value = value:gsub("\r", "")
            end
            break
        end
    end

    file:close()

    if mod_cnt == 0 then
        print("KeyRead(): Warning: key '" .. key .. "' does not exists in file '" .. filename .. "'")
    end

    return value
end

--------------------------------------------------------------------------------
-- @brief  Reads all keys and values from file
-- @param  filename     file name
-- @param  regex        regular expresion (optional)
-- @return On success key table (t[key] = value), otherwise empty table.
--------------------------------------------------------------------------------
function KeyGetList(filename, regex)
    assert(type(filename) == "string", "KeyGetList(): Invalid type of 'filename' <" .. type(filename) .. ">\n")
    assert(type(regex) == "string", "KeyGetList(): Invalid type of 'regex' <" .. type(regex) .. ">\n")

    local file = io.open(cwd .. filename, "rb")
    assert(file, "KeyGetList(): file does not exists: '" .. filename .. "'")

    local keylist = {}
    for line in file:lines() do
        if LineMatch(line, regex) then
            local k, v = LineGetKeyAndValue(line)
            keylist[k] = v
        end
    end

    file:close()

    return keylist
end

--------------------------------------------------------------------------------
-- @brief  Create new item table: {name = X, value = Y}
-- @param  None
-- @return Item table
--------------------------------------------------------------------------------
function NewItemTable()
    local tab = {}

    tab.count = 0

    -- GetCount method
    tab.GetCount = function(self)
        return self.count
    end

    -- GetIdxByName method
    tab.GetIdxByName = function(self, itemName)
        for i = 1, #self do
            if self[i].name == itemName then
                return i
            end
        end

        return 0
    end

    -- GetNameByValue method
    tab.GetNameByValue = function(self, value)
        for i = 1, #self do
            if self[i].value == value then
                return self[i].name
            end
        end

        return nil
    end

    -- GetIdxByValue method
    tab.GetIdxByValue = function(self, itemValue)
        for i = 1, #self do
            if self[i].value == itemValue then
                return i
            end
        end

        return 0
    end

    -- GetValueByIdx method
    tab.GetValueByIdx = function(self, index)
        return self[index].value
    end

    -- GetNameByIdx method
    tab.GetNameByIdx = function(self, index)
        return self[index].name
    end

    -- AddItem method
    tab.AddItem = function(self, itemName, itemValue)
        table.insert(self, { name = itemName, value = itemValue })
        self.count = self.count + 1
    end

    return tab
end

--------------------------------------------------------------------------------
-- @brief  Returns an index of the string in the selected table
-- @param  str      string to find
-- @return On success string index is returned (1..+). On error 0 is returned.
--------------------------------------------------------------------------------
table.GetStringIndex = function(tab, str)
    for i, s in ipairs(tab) do
        if s == str then
            return i
        end
    end

    return 0
end

--------------------------------------------------------------------------------
-- @brief  Function leave only unique values of the table in the order
-- @param  t        table to modify
-- @return Number of unique elements and table
--------------------------------------------------------------------------------
table.Unique = function(t)
    local unique = 0
    local ut = {}
    local u = {}

    for i, v in ipairs(t) do
        if u[v] == nil then
            u[v] = true
            table.insert(ut, v)
            unique = unique + 1
        end
    end

    return unique, ut
end

--------------------------------------------------------------------------------
-- @brief Function round value
-- @param value         value to round
-- @param precision     number of digits
-- @return Rounded value
--------------------------------------------------------------------------------
function math.round(value, precision)
    return math.floor(value * 10 ^ (precision or 0)) / 10 ^ (precision or 0)
end

--------------------------------------------------------------------------------
-- @brief  Check time value and set unit
-- @param  tm           time [s]
-- @return string with calculated time and adjusted unit
--------------------------------------------------------------------------------
function PrintTime(tm)
    if tm < 1e-6 then
        return math.round(tm * 1e9, 3) .. " ns"
    elseif tm < 1e-3 then
        return math.round(tm * 1e6, 3) .. " us"
    elseif tm < 1 then
        return math.round(tm * 1e3, 3) .. " ms"
    else
        return tm .. " s"
    end
end

--------------------------------------------------------------------------------
-- @brief  Calculate frequency unit
-- @param  freq         frequency
-- @return string of frequency with adjusted unit
--------------------------------------------------------------------------------
function PrintFrequency(freq)
    if freq < 1e3 then
        return math.round(freq, 3) .. " Hz"
    elseif freq < 1e6 then
        return math.round(freq / 1e3, 3) .. " kHz"
    else
        return math.round(freq / 1e6, 3) .. " MHz"
    end
end

--------------------------------------------------------------------------------
-- @brief  Found line by using regular expressions
-- @param  filename     file where line will be search
-- @param  startline    start line
-- @param  regex        regular expression (Lua's version)
-- @return Number of line where expression was found or 0 when not found
--------------------------------------------------------------------------------
function FindLine(filename, startline, regex)
    assert(type(filename) == "string", "LineFind(): filename is not the string type")
    assert(type(startline) == "number", "LineFind(): startline is not the number type")
    assert(type(regex) == "string", "LineFind(): regex is not the string type")

    local file = io.open(filename, "rb")
    assert(file, "LineFind(): file does not exist: " .. filename)

    file:seek("set", 0)

    local n = 1

    for line in file:lines() do
        if line:match(regex) and n >= startline then
            file:close()
            return n
        end

        n = n + 1
    end

    file:close()
    return 0
end

--------------------------------------------------------------------------------
-- @brief  Load text from selected line
-- @param  filename     file where line will be search
-- @param  linenumber   line to read
-- @return Text of line or nil if line does not exist
--------------------------------------------------------------------------------
function GetLineByNumber(filename, linenumber)
    assert(type(filename) == "string", "GetLineByNumber(): filename is not the string type")
    assert(type(linenumber) == "number", "GetLineByNumber(): linenumber is not the number type")

    local file = io.open(filename, "rb")
    assert(file, "GetLineByNumber(): file does not exist")

    file:seek("set", 0)

    local l = nil
    local n = 1

    for line in file:lines() do
        if n == linenumber then
            l = line
            break
        else
            n = n + 1
        end
    end

    file:close()

    return l
end

--------------------------------------------------------------------------------
-- @brief  Returns path base name (name of file in path)
-- @param  path
-- @return file name
--------------------------------------------------------------------------------
function basename(path)
    return path:match("^.*[\\/](.*)$")
end

--------------------------------------------------------------------------------
-- @brief  Returns path dir name (path without file)
-- @param  path
-- @return file name
--------------------------------------------------------------------------------
function dirname(path)
    return path:match("^(.*[\\/]).*$")
end

--------------------------------------------------------------------------------
-- @brief  Split string at specified speperator.
-- @param  inputstr     input string
-- @return table
--------------------------------------------------------------------------------
function string.split(inputstr, sep)
    local t = {}

    for str in inputstr:gmatch("([^" .. sep .. "]+)") do
        table.insert(t, str)
    end

    return t
end

--------------------------------------------------------------------------------
-- @brief  Save selected table to file
-- @param  tab          table to convert
-- @param  file         file to dump
-- @return On success true is returned, otherwise false
--------------------------------------------------------------------------------
function SerializeTable(tab, pretty)
    local function spairs(t, order)
        -- collect the keys
        local keys = {}
        for k in pairs(t) do
            keys[#keys + 1] = k
        end

        -- if order function given, sort by it by passing the table and keys a, b,
        -- otherwise just sort the keys
        if order then
            table.sort(keys, function(a, b)
                return order(t, a, b)
            end)
        else
            table.sort(keys)
        end

        -- return the iterator function
        local i = 0
        return function()
            i = i + 1
            if keys[i] then
                return keys[i], t[keys[i]]
            end
        end
    end

    local savedTables = {} -- used to record tables that have been saved, so that we do not go into an infinite recursion
    local outFuncs = {
        ["string"] = function(value)
            return string.format("%q", value)
        end,
        ["boolean"] = function(value)
            if value then
                return "true"
            else
                return "false"
            end
        end,
        ["number"] = function(value)
            return string.format("%f", value)
        end,
    }

    local outFuncsMeta = {
        __index = function(t, k)
            print("Invalid Type For SaveTable(): " .. k)
            os.exit()
        end,
    }

    local indent = ""

    setmetatable(outFuncs, outFuncsMeta)

    local tableOut = function(value)
        if pretty then
            indent = indent .. "\t"
        end

        if savedTables[value] then
            print("There is a cyclical reference (table value referencing another table value) in this set.")
            os.exit()
        end

        local outValue = function(value)
            return outFuncs[type(value)](value)
        end

        local out = "{"

        if pretty then
            out = out .. "\n"
        end

        for i, v in spairs(value) do
            out = out .. indent .. "[" .. outValue(i) .. "]=" .. outValue(v) .. ";"
            if pretty then
                out = out .. "\n"
            end
        end

        savedTables[value] = true --record that it has already been saved

        if pretty then
            indent = indent:sub(1, -2)
        end

        return out .. indent .. "}"
    end

    outFuncs["table"] = tableOut

    if type(tab) == "table" then
        return tableOut(tab)
    end

    return ""
end

--------------------------------------------------------------------------------
-- @brief  Save selected table to file
-- @param  tab          table to convert
-- @param  file         file to dump
-- @return On success true is returned, otherwise false
--------------------------------------------------------------------------------
function SaveTable(tab, file)
    if type(tab) == "table" and type(file) == "string" then
        local f = io.open(file, "wb")
        if f then
            f:write(SerializeTable(tab, true))
            f:close()
            return true
        end
    end

    return false
end
table.save = SaveTable

--------------------------------------------------------------------------------
-- @brief  Load table from file
-- @param  file         file to read from
-- @return On success table is returned, otherwise nil
--------------------------------------------------------------------------------
function LoadTable(file)
    if type(file) == "string" then
        local f = io.open(file, "rb")
        if f then
            local input = f:read("*all")
            f:close()

            if pcall(load("return " .. input)) then
                local table = load("return " .. input)()
                if type(table) == "table" then
                    return table
                end
            end
        end
    end

    return nil
end
table.load = LoadTable

--------------------------------------------------------------------------------
-- @brief  Run dialog.
-- @param  command  command
-- @return None
--------------------------------------------------------------------------------
function RunDialog(command)
    local full_cmd = "dialog " .. command .. " --stdout"
    local handle = io.popen(full_cmd)
    if handle == nil then
        return false, ""
    end

    local result = handle:read("*a")

    local success, reason, code = handle:close()
    if result then
        result = result:gsub("^%s*(.-)%s*$", "%1")
    end

    return success, result
end

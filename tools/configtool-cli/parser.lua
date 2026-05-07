--[[============================================================================
@file    parser.lua

@author  Daniel Zorychta

@brief   Parser of configuration headers

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

--============================================================================--
-- PRIVATE FUNCTIONS
--============================================================================--

--------------------------------------------------------------------------------
-- @brief  Function create error message
-- @param  msg              message to print
-- @return None
--------------------------------------------------------------------------------
local function GetErrorMsg(self, msg)
    return "[Error] " .. self._FileName .. ":" .. self._ChunkBegin .. "-" .. self._ChunkEnd .. ": " .. msg
end

--------------------------------------------------------------------------------
-- @brief  Function create warning message
-- @param  msg              message to print
-- @return None
--------------------------------------------------------------------------------
local function GetWarningMsg(self, msg)
    return "[Warning] " .. self._FileName .. ":" .. self._ChunkBegin .. "-" .. self._ChunkEnd .. ": " .. msg
end

--------------------------------------------------------------------------------
-- @brief  Function prints error and close application
-- @param  msg              message to print
-- @return None
--------------------------------------------------------------------------------
local function PrintError(self, msg)
    print(GetErrorMsg(self, msg))
    ShowErrorMessage("Parsing error", GetErrorMsg(self, msg), false)

    local scr = '--title "Script error!" --msgbox ' .. "'\n" .. msg .. "' 0 0"
    RunDialog(scr)

    self._script.finish = true
end

--------------------------------------------------------------------------------
-- @brief  Function prints warning
-- @param  msg              message to print
-- @return None
--------------------------------------------------------------------------------
local function PrintWarning(self, msg)
    print(GetWarningMsg(self, msg))
    ShowInfoMessage("Parsing warning", GetWarningMsg(self, msg))

    local scr = '--title "Script warning!" --msgbox ' .. "'\n" .. msg .. "' 10 40"
    RunDialog(scr)
end

--------------------------------------------------------------------------------
-- @brief  Parser assert
-- @param  msg              message to print
-- @return None
--------------------------------------------------------------------------------
local function ParserAssert(self, test, msg)
    if test == false then
        print(GetErrorMsg(self, msg))
        os.exit()
    end
end

--------------------------------------------------------------------------------
-- @brief  Call selected parser event.
-- @param  event
-- @return None
--------------------------------------------------------------------------------
local function CallParserEvent(self, event)
    if self._Event[event] then
        return self._Event[event]()
    end
end

--------------------------------------------------------------------------------
-- @brief  Call selected flag event.
-- @param  event
-- @return None
--------------------------------------------------------------------------------
local function CallFlagEvent(flag, event)
    if flag.Event[event] then
        flag.Event[event]()
    end
end

--------------------------------------------------------------------------------
-- @brief  Call loaded script
-- @param  self
-- @param  script   script to run
-- @return None
--------------------------------------------------------------------------------
local function ExecuteScript(self, script)
    local status, err = pcall(load("local this = _SCRIPT_SELF " .. script))
    if status == false then
        local chunkLine = err:gsub("%[.*%]:", ""):match("(%d+)", "1")
        if chunkLine == nil then
            chunkLine = 0
        end

        local errMsg = err:gsub("%[.*%]", ""):gsub(":%d+:%s+", "")

        PrintError(self, self._FileName .. ": " .. math.floor(self._ChunkBegin + chunkLine) .. ": " .. errMsg)
    end

    return ""
end

--------------------------------------------------------------------------------
-- @brief  Create GUI.
-- @param  self     parser
-- @return Path to next file.
--------------------------------------------------------------------------------
local function ManageGUI(self, startEntry)
    local function FormatValue(flag, input)
        return flag.ValueFormatterFunc(flag.ValueFormatterCtx, input)
    end

    while true do
        local menu = "--title '"
            .. self._Layout.Title
            .. "' --default-item '"
            .. startEntry
            .. "' "
            .. " --cancel-label Back --help-button --help-label Help --menu '"
            .. self._FileName
            .. "\n\n"
            .. self._Layout.Brief
            .. "' 0 0 0 "

        for _, w in ipairs(self._ui) do
            if w.WidgetType == "Label" then
                menu = menu .. '"' .. w.text .. '" "" '
            elseif w.WidgetType == "Combobox" then
                local item = w.flag.Item:GetNameByValue(w.flag:GetValue())
                if item == nil then
                    item = "<UNKNOWN:" .. w.flag:GetValue() .. ">"
                end

                menu = menu .. '"' .. w.text .. '" "' .. FormatValue(w.flag, item) .. '" '
            elseif w.WidgetType == "Submenu" then
                menu = menu .. '"' .. w.text .. '" "" '
            elseif w.WidgetType == "Editline" then
                menu = menu .. "'" .. w.text .. "' '" .. FormatValue(w.flag, w.flag:GetValue()) .. "' "
            elseif w.WidgetType == "Spinbox" then
                menu = menu .. '"' .. w.text .. '" "' .. FormatValue(w.flag, w.flag:GetValue()) .. '" '
            elseif w.WidgetType == "Textbox" then
                menu = menu .. '"' .. w.text .. '" "' .. FormatValue(w.flag, w.flag:GetValue()) .. '" '
            end
        end

        --print(menu)
        --os.execute("sleep 10")

        local state, result = RunDialog(menu)
        if state == true then
            for _, w in ipairs(self._ui) do
                if w.text == result then
                    if w.WidgetType == "Submenu" then
                        CallFlagEvent(w.flag, "clicked")
                        return w.link, w.text
                    elseif w.WidgetType == "Editline" then
                        local scr = '--ok-label Save --cancel-label Back --inputbox "'
                            .. w.text
                            .. '" 0 0 '
                            .. "'"
                            .. (w.quotation and w.flag:GetValue():gsub('^"', ""):gsub('"$', "") or w.flag:GetValue())
                            .. "'"

                        local s, r = RunDialog(scr)
                        if s == true then
                            if w.quotation then
                                r = '"' .. r .. '"'
                            end
                            w.flag.SetValue(r)
                            CallFlagEvent(w.flag, "clicked")
                        end
                    elseif w.WidgetType == "Combobox" then
                        local scr = '--no-tags --ok-label Save --cancel-label Back --radiolist "'
                            .. w.text
                            .. '" '
                            .. "0 0 0 "

                        local entries = ""
                        local defaultItem = "~"
                        for i = 1, w.flag.Item:GetCount() do
                            local value = w.flag.Item:GetValueByIdx(i)
                            local name = w.flag.Item:GetNameByIdx(i)
                            entries = entries .. '"' .. value .. '" "' .. name .. '" '

                            if w.flag:GetValue() == value then
                                entries = entries .. " on "
                                defaultItem = value
                            else
                                entries = entries .. " off "
                            end
                        end

                        scr = "--default-item '" .. defaultItem .. "' " .. scr .. entries

                        local s, r = RunDialog(scr)
                        if s == true then
                            w.flag.SetValue(r)
                            CallFlagEvent(w.flag, "clicked")
                        end
                    elseif w.WidgetType == "Spinbox" then
                        while true do
                            local scr = '--ok-label Save --cancel-label Back --inputbox "'
                                .. w.text
                                .. "\n("
                                .. w.min
                                .. ".."
                                .. w.max
                                .. ")"
                                .. '" 0 0 '
                                .. "'"
                                .. w.flag:GetValue()
                                .. "'"

                            local s, r = RunDialog(scr)
                            if s == true then
                                local num = tonumber(r)
                                if type(num) == "number" then
                                    if num < w.min then
                                        num = w.min
                                    elseif num > w.max then
                                        num = w.max
                                    end

                                    w.flag.SetValue(tostring(num))
                                    CallFlagEvent(w.flag, "clicked")

                                    break
                                end
                            else
                                break
                            end
                        end
                    end

                    startEntry = w.text
                    break
                end
            end
        elseif result:match("HELP") then
            local help_of = result:gsub("HELP ", "")
            for _, w in ipairs(self._ui) do
                if w.text == help_of then
                    if w.flag.ToolTip ~= nil then
                        local scr = '--title "' .. w.text .. '" --msgbox ' .. "'\n" .. w.flag.ToolTip .. "' 10 40"
                        RunDialog(scr)
                    end

                    startEntry = w.text
                    break
                end
            end
        else
            CallParserEvent(self, "BackClicked")

            if self:IsModified() then
                local scr = '--title "'
                    .. self._Layout.Title
                    .. '" --yesno '
                    .. "'The file was modified.\n\nDo you want to save changes?' 0 0"

                local s = RunDialog(scr)
                if s == true then
                    self:Save()
                end
            end
            break
        end
    end

    return nil
end

--------------------------------------------------------------------------------
-- @brief  Function parse selected file
-- @param  self         class object
-- @param  startEntry   start entry
-- @return None
--------------------------------------------------------------------------------
local function RunParser(self, startEntry)
    _SCRIPT_SELF = self._script

    self._ui = {}

    local file = io.open(GetConfigPath() .. self._FileName, "rb")
    if file == nil then
        PrintError(self, "File not exist: " .. self._FileName)
        return nil
    end

    local scriptArea = false
    local script = ""
    local lineNo = 1

    for line in file:lines() do
        if line:match("^%s*#%s*define%s+" .. self._KeyRegExp) then
            local currentFlag, value = line:match("^%s*#%s*define%s+(" .. self._KeyRegExp .. ")%s+(.*)")
            self._CurrentFlag = currentFlag

            if self._CurrentFlag ~= nil or value ~= nil then
                self._Flag[self._CurrentFlag] = {}
                self._Flag[self._CurrentFlag].Value = value
                self._Flag[self._CurrentFlag].OrginalValue = value
                self._Flag[self._CurrentFlag].GetValue = function()
                    return self._Flag[self._CurrentFlag].Value
                end
                self._Flag[self._CurrentFlag].SetValue = function(value)
                    self._Flag[self._CurrentFlag].Value = value
                end

                script = ExecuteScript(self, script)
            end
        elseif line:match("^/%*%-%-") or line:match("^#/%*%-%-") then
            script = ""
            scriptArea = true
            self._ChunkBegin = lineNo
        elseif line:match("^%-%-%*/") or line:match("^#%-%-%*/") then
            scriptArea = false
            self._ChunkEnd = lineNo
        elseif line:match("^%+%+%*/") or line:match("^#%+%+%*/") then
            scriptArea = false
            self._ChunkEnd = lineNo
            script = ExecuteScript(self, script)
        elseif scriptArea == true then
            if line:match("^(#)") then
                script = script .. line:match("^#(.*)") .. "\n"
            else
                script = script .. line .. "\n"
            end
        end

        lineNo = lineNo + 1

        if self._script.finish == true then
            break
        end
    end

    self._script.finish = false

    file:close()

    _SCRIPT_SELF = nil

    if self._ui then
        return ManageGUI(self, startEntry)
    end

    return nil
end

--------------------------------------------------------------------------------
-- @brief  Adds widget to panel (by using sizer)
-- @param  self             class object
-- @param  widget           widget to add
-- @param  flags            extra flags
-- @return None
--------------------------------------------------------------------------------
local function AddToWindow(self, widget, flags)
    table.insert(self._ui, widget)
end

--------------------------------------------------------------------------------
-- @brief  Create Combobox widget
-- @param  self         class object
-- @param  FLAG         flag object
-- @param  text         widget text
-- @return None
--------------------------------------------------------------------------------
local function GenericAddCombobox(self, FLAG, text)
    ParserAssert(
        self,
        type(text) == "string",
        "In one of following methods:\n"
            .. '1. this:AddWidget("Combobox", text)\n'
            .. '2. this:AddExtraWidget("Combobox", "widgetName", text)\n'
            .. "Argument 'text' must be a string; got "
            .. type(text)
            .. " instead."
    )

    AddToWindow(self, { WidgetType = "Combobox", text = text, flag = FLAG })
end

--------------------------------------------------------------------------------
-- @brief  Create Label widget
-- @param  self         class object
-- @param  FLAG         flag object
-- @param  text         widget text
-- @return None
--------------------------------------------------------------------------------
local function GenericAddLabel(self, FLAG, text)
    ParserAssert(
        self,
        type(text) == "string",
        "In one of following methods:\n"
            .. '1. this:AddWidget("Label", text)\n'
            .. '2. this:AddExtraWidget("Label", "widgetName", text)\n'
            .. "Argument 'text' must be a string; got "
            .. type(text)
            .. " instead."
    )

    AddToWindow(self, { WidgetType = "Label", text = text, flag = FLAG })
end

--------------------------------------------------------------------------------
-- @brief  Create Value widget
-- @param  self         class object
-- @param  FLAG         flag object
-- @return None
--------------------------------------------------------------------------------
local function GenericAddValue(self, FLAG)
    -- all basic methodes defined earlier
end

--------------------------------------------------------------------------------
-- @brief  Create Submenu widget
-- @param  self         class object
-- @param  FLAG         flag object
-- @param  text         widget text
-- @param  path         script path
-- @return None
--------------------------------------------------------------------------------
local function GenericAddSubmenu(self, FLAG, text, link)
    ParserAssert(
        self,
        type(text) == "string",
        "In one of following methods:\n"
            .. '1. this:AddWidget("Submenu", text, link)\n'
            .. '2. this:AddExtraWidget("Submenu", "widgetName", text, link)\n'
            .. "Argument 'text' must be a string; got "
            .. type(text)
            .. " instead."
    )

    ParserAssert(
        self,
        type(link) == "string",
        "In one of following methods:\n"
            .. '1. this:AddWidget("Label", text, link)\n'
            .. '2. this:AddExtraWidget("Label", "widgetName", text, link)\n'
            .. "Argument 'link' must be a string; got "
            .. type(text)
            .. " instead."
    )

    AddToWindow(self, { WidgetType = "Submenu", text = text, flag = FLAG, link = link })
end

--------------------------------------------------------------------------------
-- @brief  Create Editline widget
-- @param  self         class object
-- @param  FLAG         flag object
-- @param  quotation    value in quotation
-- @param  text         widget text
-- @return None
--------------------------------------------------------------------------------
local function GenericAddEditline(self, FLAG, quotation, text)
    ParserAssert(
        self,
        type(quotation) == "boolean",
        "In one of following methods:\n"
            .. '1. this:AddWidget("Editline", quotation, text)\n'
            .. '2. this:AddExtraWidget("Label", "widgetName", quotation, text)\n'
            .. "Argument 'quotation' must be a boolean; got "
            .. type(text)
            .. " instead."
    )

    ParserAssert(
        self,
        type(text) == "string",
        "In one of following methods:\n"
            .. '1. this:AddWidget("Editline", quotation, text)\n'
            .. '2. this:AddExtraWidget("Label", "widgetName", quotation, text)\n'
            .. "Argument 'text' must be a string; got "
            .. type(text)
            .. " instead."
    )

    AddToWindow(self, { WidgetType = "Editline", text = text, flag = FLAG, quotation = quotation })
end

--------------------------------------------------------------------------------
-- @brief  Create Editline widget
-- @param  self         class object
-- @param  FLAG         flag object
-- @param  minValue     minimal value
-- @param  maxValue     maximal value
-- @param  text         widget text
-- @return None
--------------------------------------------------------------------------------
local function GenericAddSpinbox(self, FLAG, minValue, maxValue, text)
    ParserAssert(
        self,
        type(minValue) == "number",
        "In one of following methods:\n"
            .. '1. this:AddWidget("Spinbox", minValue, maxValue, text)\n'
            .. '2. this:AddExtraWidget("Spinbox", "widgetName", minValue, maxValue, text)\n'
            .. "Argument 'minValue' must be a number; got "
            .. type(text)
            .. " instead."
    )

    ParserAssert(
        self,
        type(maxValue) == "number",
        "In one of following methods:\n"
            .. '1. this:AddWidget("Spinbox", minValue, maxValue, text)\n'
            .. '2. this:AddExtraWidget("Spinbox", "widgetName", minValue, maxValue, text)\n'
            .. "Argument 'maxValue' must be a number; got "
            .. type(text)
            .. " instead."
    )

    ParserAssert(
        self,
        type(text) == "string",
        "In one of following methods:\n"
            .. '1. this:AddWidget("Spinbox", minValue, maxValue, text)\n'
            .. '2. this:AddExtraWidget("Spinbox", "widgetName", minValue, maxValue, text)\n'
            .. "Argument 'text' must be a string; got "
            .. type(text)
            .. " instead."
    )

    AddToWindow(self, { WidgetType = "Spinbox", text = text, flag = FLAG, min = minValue, max = maxValue })
end

--------------------------------------------------------------------------------
-- @brief  Function creates widgets
-- @param  widgetType       Widget Type: Combobox, Label
-- @param  widgetName       Widget Name (or flag name)
-- @param  widgetArg1       Argument 1 (optional)
-- @param  widgetArg2       Argument 2 (optional)
-- @param  widgetArg3       Argument 3 (optional)
-- @param  widgetArg4       Argument 4 (optional)
-- @return None
--------------------------------------------------------------------------------
local function GenericAddWidget(self, widgetType, widgetName, Arg1, Arg2, Arg3, Arg4)
    -- widget shortcut
    local FLAG = self._Flag[widgetName]

    -- create flag default structure
    FLAG.Item = NewItemTable()

    FLAG.ValueFormatterFunc = function(id, input)
        return input
    end

    FLAG.ValueFormatterCtx = ""

    FLAG.AddItem = function(itemName, itemValue)
        FLAG.Item:AddItem(itemName, itemValue)
    end

    FLAG.GetValue = function()
        return FLAG.Value
    end

    FLAG.SetValue = function(value)
        FLAG.Value = value
    end

    FLAG.Event = {}
    FLAG.SetEvent = function(event, func)
        FLAG.Event[event] = func
    end

    FLAG.Enable = function(en) end

    FLAG.SetToolTip = function(tip)
        FLAG.ToolTip = tip
    end

    -- widget creator lookup table
    local ADD_WIDGET = {}
    ADD_WIDGET["Combobox"] = GenericAddCombobox
    ADD_WIDGET["Label"] = GenericAddLabel
    ADD_WIDGET["Value"] = GenericAddValue
    ADD_WIDGET["Submenu"] = GenericAddSubmenu
    ADD_WIDGET["Editline"] = GenericAddEditline
    ADD_WIDGET["Spinbox"] = GenericAddSpinbox

    if ADD_WIDGET[widgetType] then
        ADD_WIDGET[widgetType](self, FLAG, Arg1, Arg2, Arg3, Arg4)
    else
        PrintError(
            self,
            "Unknown widget: "
                .. widgetType
                .. "\n"
                .. "Available widgets:\n"
                .. "  Combobox, {widget name}, text\n"
                .. "  Label, {widget name}, text\n"
                .. "  Value, {widget name} (no GUI widget)\n"
                .. "  Submenu, {widget name}, text\n"
                .. "  Editline {widget name}, quotation, text\n"
                .. "  Spinbox {widget name}, minValue, maxValue, text\n"
                .. "\n"
                .. "Argument in {} defines name of widget and is accessible "
                .. "only by using AddExtraWidget() function.\n"
                .. "Argument in [] is optional."
        )
    end
end

--============================================================================--
-- FUNCTIONS ACCESSABLE FROM CONFIGURATION FILES
--============================================================================--
--------------------------------------------------------------------------------
-- @brief  Function creates widget in selected flag (current flag)
-- @param  widgetType       widget type
-- @param  Argx             widget arguments
-- @return None
--------------------------------------------------------------------------------
local function AddWidget(this, widgetType, Arg1, Arg2, Arg3, Arg4)
    local self = this.self

    ParserAssert(
        self,
        type(widgetType) == "string",
        "AddWidget(widgetType[, ...]): widgetType: string expected (got " .. type(widgetType) .. ")"
    )

    if self._Flag[self._CurrentFlag].wx == nil then
        GenericAddWidget(self, widgetType, self._CurrentFlag, Arg1, Arg2, Arg3, Arg4)
    else
        PrintError(self, "Flag can be represented only by one widget!")
    end
end

--------------------------------------------------------------------------------
-- @brief  Function adds extra widget with selected name
-- @param  widgetType       widget type
-- @param  widgetName       widget name
-- @param  arg1             argument 1 (optional)
-- @param  arg2             argument 2 (optional)
-- @param  arg3             argument 3 (optional)
-- @param  arg4             argument 4 (optional)
-- @return None
--------------------------------------------------------------------------------
local function AddExtraWidget(this, widgetType, widgetName, arg1, arg2, arg3, arg4)
    local self = this.self

    ParserAssert(
        self,
        type(widgetType) == "string",
        "AddExtraWidget(widgetType, widgetName[, ...]): widgetType: string expected (got " .. type(widgetType) .. ")"
    )
    ParserAssert(
        self,
        type(widgetName) == "string",
        "AddExtraWidget(widgetType, widgetName[, ...]): widgetName: string expected (got " .. type(widgetName) .. ")"
    )

    if self._Flag[widgetName] == nil then
        self._Flag[widgetName] = {}
        self._Flag[widgetName].Value = ""
        self._Flag[widgetName].OrginalValue = ""

        GenericAddWidget(self, widgetType, widgetName, arg1, arg2, arg3, arg4)
    else
        PrintError(self, "Widget: '" .. widgetName .. "' is already defined")
    end
end

--------------------------------------------------------------------------------
-- @brief  Function read flag value in current file
-- @param  flagName         flag name
-- @param  filePath         external file (optional)
-- @return Flag value
--------------------------------------------------------------------------------
local function GetFlagValue(this, flagName, filePath)
    local self = this.self

    ParserAssert(
        self,
        type(flagName) == "string",
        "GetFlagValue(flagName[, filePath]): flagName: string expected (got " .. type(flagName) .. ")"
    )

    if type(filePath) == "string" then
        return KeyRead(filePath, flagName)
    else
        if flagName == "" then
            return nil
        elseif self._Flag[flagName] then
            return self._Flag[flagName].GetValue()
        else
            return nil
        end
    end
end

--------------------------------------------------------------------------------
-- @brief  Function get flag list according to regular expression
-- @param  filename         file where flags are read
-- @param  regexp           flag regular expression
-- @return Flag list (t[key] = value format).
--------------------------------------------------------------------------------
local function GetFlagsList(this, filename, regexp)
    local self = this.self

    ParserAssert(
        self,
        type(filename) == "string",
        "GetFlagsList(filename, regexp): filename: string expected (got " .. type(filename) .. ")"
    )
    ParserAssert(
        self,
        type(regexp) == "string",
        "GetFlagsList(filename, regexp): regexp: string expected (got " .. type(regexp) .. ")"
    )

    return KeyGetList(filename, regexp)
end

--------------------------------------------------------------------------------
-- @brief  Function set flag value in current file
-- @param  flagName         flag name
-- @param  value            value to set
-- @param  filePath         external file (optional)
-- @return None
--------------------------------------------------------------------------------
local function SetFlagValue(this, flagName, value, filePath)
    local self = this.self

    ParserAssert(
        self,
        type(flagName) == "string",
        "SetFlagValue(flagName, value[, filePath]): flagName: string expected (got " .. type(flagName) .. ")"
    )

    ParserAssert(
        self,
        type(value) == "string",
        "SetFlagValue(flagName, value[, filePath]): value: string expected (got " .. type(value) .. ")"
    )

    if type(filePath) == "string" then
        KeyWrite(filePath, flagName, value)
    else
        -- if function does not exists flag is not handled by widget
        if self._Flag[flagName].SetValue then
            self._Flag[flagName].SetValue(value)
        end
    end
end

--------------------------------------------------------------------------------
-- @brief  Function set flag value in current file
-- @param  msg      message
-- @return None
--------------------------------------------------------------------------------
local function Debug(this, msg)
    local self = this.self

    RunDialog("--title Debug --msgbox '" .. msg .. "' 0 0")
end

--------------------------------------------------------------------------------
-- @brief  Function set value compositor function.
-- @param  ctx      context
-- @param  func     compositor function
-- @return None
--------------------------------------------------------------------------------
local function SetValueFormatter(this, ctx, func)
    local self = this.self

    if type(func) == "function" then
        if self._Flag[self._CurrentFlag] ~= nil then
            self._Flag[self._CurrentFlag].ValueFormatterFunc = func
            self._Flag[self._CurrentFlag].ValueFormatterCtx = ctx
        else
            PrintError(self, "SetValueFormatter(): access to unknown flag.")
        end
    else
        PrintError(
            self,
            "Used: SetValueFormatter("
                .. type(ctx)
                .. ", "
                .. type(func)
                .. "): incorrect arguments, use:\n"
                .. "1. SetValueFormatter(any:ctx, function:func)\n"
        )
    end
end

--------------------------------------------------------------------------------
-- @brief  Function set tooltip for selected widget
-- @param  arg1             tip message/flag name
-- @param  arg2             tip message (if arg1 is flag name)
-- @return None
--------------------------------------------------------------------------------
local function SetToolTip(this, arg1, arg2)
    local self = this.self

    if type(arg1) == "string" and type(arg2) == "string" then
        local flagName = arg1
        local tipMessage = arg2

        if self._Flag[flagName] ~= nil then
            self._Flag[flagName].SetToolTip(tipMessage)
        else
            PrintError(self, "SetToolTip(): flag '" .. flagName .. "' does not exists.")
        end
    elseif type(arg1) == "string" then
        local tipMessage = arg1

        if self._Flag[self._CurrentFlag] ~= nil then
            self._Flag[self._CurrentFlag].SetToolTip(tipMessage)
        else
            PrintError(self, "SetToolTip(): try to access to unknown flag.")
        end
    else
        PrintError(
            self,
            "Used: SetToolTip("
                .. type(arg1)
                .. ", "
                .. type(arg2)
                .. "): incorrect arguments, use:\n"
                .. "1. SetToolTip(string:TipMessage)\n"
                .. "2. SetToolTip(string:FlagName, string:TipMessage)\n"
        )
    end
end

--------------------------------------------------------------------------------
-- @brief  Function add item to current widget (e.g. combobox, etc)
-- @param  itemName         item name (visible in widget)
-- @param  itemValue        item value (value stored in configuration file)
-- @param  flagName         flag name (optional)
-- @return None
--------------------------------------------------------------------------------
local function AddItem(this, itemName, itemValue, flagName)
    local self = this.self

    ParserAssert(
        self,
        type(itemName) == "string",
        "AddItem(itemName, itemValue): itemName: string expected (got " .. type(itemName) .. ")"
    )
    ParserAssert(
        self,
        type(itemValue) == "string",
        "AddItem(itemName, itemValue): itemValue: string expected (got " .. type(itemValue) .. ")"
    )

    if type(flagName) == "string" then
        if self._Flag[flagName] ~= nil then
            self._Flag[flagName].AddItem(itemName, itemValue)
        else
            PrintWarning(self, "AddItem(): flag '" .. flagName .. "' does not exists")
        end
    else
        self._Flag[self._CurrentFlag].AddItem(itemName, itemValue)
    end
end

--------------------------------------------------------------------------------
-- @brief  Function set event action of current widget
-- @param  event            event ("clicked", "PreSave", "PostSave", "PreDiscard", "PostDiscard", ...)
-- @param  func             function to handle event
-- @return None
--------------------------------------------------------------------------------
local function SetEvent(this, event, arg1, arg2)
    local self = this.self

    if
        event == "PreSave"
        or event == "PostSave"
        or event == "PreDiscard"
        or event == "PostDiscard"
        or event == "BackClicked"
    then
        self._Event[event] = arg1
    elseif event == "ConfigExport" then
        self._ExportEvent = arg1
    elseif event == "ConfigImport" then
        self._ImportEvent = arg1
    else
        if type(event) == "string" and type(arg1) == "string" and type(arg2) == "function" then
            self._Flag[arg1].SetEvent(event, arg2)
        elseif type(event) == "string" and type(arg1) == "function" then
            self._Flag[self._CurrentFlag].SetEvent(event, arg1)
        else
            PrintError(
                self,
                "Function SetEvent() called with wrong arguments. Use as follow:\n"
                    .. "1: SetEvent(eventName:string, widgetName:string, function)\n"
                    .. "2: SetEvent(eventName:string, function)\n\n"
                    .. "Events:\n"
                    .. "clicked      - object clicked\n"
                    .. "PreSave      - event is called before save procedure\n"
                    .. "PostSave     - event is called after save procedure\n"
                    .. "PreDiscard   - event is called before discard procedure\n"
                    .. "PostDiscard  - event is called after discard procedure\n"
                    .. "ConfigExport - event is called before configuration be exported\n"
                    .. "ConfigImport - event is called before configuration be imported\n"
            )
        end
    end
end

--------------------------------------------------------------------------------
-- @brief  Function set title and brief.
-- @param  title            title
-- @param  brief            brief
-- @return None
--------------------------------------------------------------------------------
local function SetTitle(this, title, brief)
    local self = this.self

    ParserAssert(
        self,
        type(title) == "string",
        "SetTitle(title, brief): title: string expected (got " .. type(title) .. ")"
    )

    ParserAssert(
        self,
        type(brief) == "string",
        "SetTitle(title, brief): brief: string expected (got " .. type(brief) .. ")"
    )

    self._Layout = {
        Title = title,
        Brief = brief,
    }
end

--============================================================================--
-- PARSER CLASS FUNCTIONS
--============================================================================--
--------------------------------------------------------------------------------
-- @brief  Method parse selected file
-- @param  self
-- @param  fileName
-- @return true on success, otherwise false
--------------------------------------------------------------------------------
local function Parse(self, fileName)
    self._FileName = iff(type(fileName) == "string", fileName, self._FileName)

    local history = {}
    table.insert(history, { path = fileName, startEntry = "~" })

    local state = {}

    while history[#history] ~= nil do
        self = CreateParser(history[#history].path, state)
        if self ~= nil then
            local path, entry = RunParser(self, history[#history].startEntry)
            if type(path) == "string" then
                history[#history].startEntry = entry
                table.insert(history, { path = path, startEntry = "~" })
            else
                table.remove(history)
            end
        else
            break
        end
    end
end

--------------------------------------------------------------------------------
-- @brief  Method save all values
-- @param  self
-- @return true on success, otherwise false
--------------------------------------------------------------------------------
local function Save(self)
    if CallParserEvent(self, "PreSave") == false then
        return false
    end

    for key, val in pairs(self._Flag) do
        if key:match("(" .. self._KeyRegExp .. ")") then
            if val.OrginalValue ~= val.Value then
                KeyWrite(self._FileName, key, val.Value)
                val.OrginalValue = val.Value
            end
        end
    end

    CallParserEvent(self, "PostSave")

    return true
end

--------------------------------------------------------------------------------
-- @brief  Method discard values
-- @param  self
-- @return None
--------------------------------------------------------------------------------
local function Discard(self)
    CallParserEvent(self, "PreDiscard")

    for key, val in pairs(self._Flag) do
        if key:match("(" .. self._KeyRegExp .. ")") then
            if val.OrginalValue ~= val.Value then
                SetFlagValue(self._script, key, val.OrginalValue)
            end
        end
    end

    CallParserEvent(self, "PostDiscard")
end

--------------------------------------------------------------------------------
-- @brief  Method check if values are modified
-- @param  self
-- @return true on success, otherwise false
--------------------------------------------------------------------------------
local function IsModified(self)
    for key, val in pairs(self._Flag) do
        if key:match("(" .. self._KeyRegExp .. ")") then
            if val.OrginalValue ~= val.Value then
                -- PrintWarning(self, key..": \""..val.OrginalValue.."\" -> \""..val.Value.."\"")
                return true
            end
        end
    end

    return false
end

--------------------------------------------------------------------------------
-- @brief  Function create new configuration parser
-- @param  None
-- @return None
--------------------------------------------------------------------------------
function CreateParser(fileName, state)
    local self = {}

    self._ui = {}

    -- class objects
    self._KeyRegExp = "[a-zA-Z0-9_]+"
    self._CurrentFlag = ""
    self._ChunkBegin = 1
    self._ChunkEnd = 1
    self._FileName = fileName
    self._Event = {}
    self._Flag = {}
    self._ImportEvent = function() end
    self._ExportEvent = function() end
    self._Layout = { Title = "Menu", Brief = "" }

    -- script functions
    self._script = {}
    self._script.self = self
    self._script.finish = false
    self._script.AddWidget = AddWidget
    self._script.SetTitle = SetTitle
    self._script.SetToolTip = SetToolTip
    self._script.GetFlagValue = GetFlagValue
    self._script.SetFlagValue = SetFlagValue
    self._script.GetFlagsList = GetFlagsList
    self._script.AddItem = AddItem
    self._script.SetEvent = SetEvent
    self._script.AddExtraWidget = AddExtraWidget
    self._script.Debug = Debug
    self._script.SetValueFormatter = SetValueFormatter
    self._script.state = state

    self._script.Finish = function()
        self._script.finish = true
    end

    self._script.Save = function()
        self:Save()
    end

    self._script.SetKeyRegExp = function(_, regExp)
        self._KeyRegExp = regExp
    end

    -- methodes
    self.Parse = Parse
    self.Save = Save
    self.Discard = Discard
    self.IsModified = IsModified

    self.GetFileName = function(self)
        return self._FileName
    end

    self.CallEvent = CallParserEvent

    return self
end

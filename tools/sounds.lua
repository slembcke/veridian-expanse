--[[
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
]]

local sound_enums = io.open("sound_enums.inc", "w")
local sound_defs = io.open("sound_defs.inc", "w")

for _, filename in ipairs(arg) do
	name = filename:match(".*/(.*).ogg"):upper()
	sound_enums:write(string.format("DRIFT_SFX_%s,\n", name))
	
	local template = "[DRIFT_SFX_%s] = \"%s\",\n"
	sound_defs:write(template:format(name, filename:match("resources/(.*)")))
end

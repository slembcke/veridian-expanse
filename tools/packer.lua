local run = dbg and dbg.call or function(f) f() end

run(function()

local json = require 'json'
local rects = {}
local images = {}

for _, filename in ipairs(arg) do
	local basename = filename:gsub("%.ase", "")
	local file = io.open(basename..".json")
	local data = json.decode(file:read("a"))
	
	local atlas = filename:match("^.*/(ATLAS.*)%.ase")
	if atlas then
		assert(#data.frames == 1, "Atlas files cannot have multiple frames")
		atlas = atlas:upper()
		
		for _, slice in ipairs(data.meta.slices) do
			local rect = slice.keys[1].bounds
			rect.name = slice.name:upper()
			rect.atlas = atlas
			
			local pivot = slice.keys[1].pivot
			if pivot then rect.ax, rect.ay = pivot.x, pivot.y end
			
			table.insert(rects, rect)
		end
	else
		assert(#data.meta.slices == 1, string.format("Sheet '%s' must have 1 slice. (has %d)", filename, #data.meta.slices))
		table.insert(images, basename)
		
		local slice = data.meta.slices[1]
		local pivot = slice.keys[1].pivot
		
		local name = slice.name
		for i, frame in ipairs(data.frames) do
			local rect = frame.frame
			if pivot then rect.ax, rect.ay = pivot.x, pivot.y end
			
			rect.pack = true
			rect.name = string.format("%s%02d", name:upper(), i - 1)
			rect.image_idx = #images
			
			
			table.insert(rects, rect)
		end
	end
end

local atlases = gen_atlases(images, rects)
table.sort(rects, function(a, b) return a.name < b.name end)

local sprite_enums = io.open("sprite_enums.inc", "w")
local sprite_defs = io.open("sprite_defs.inc", "w")
for _, r in ipairs(rects) do
	sprite_enums:write(string.format("DRIFT_SPRITE_%s,\n", r.name))
	
	local x1, y1 = r.x + r.w - 1, r.y + r.h - 1
	local ax, ay = r.ax or r.w//2, r.ay or r.h//2
	local template = "[DRIFT_SPRITE_%s] = {.layer = DRIFT_%s, .bounds = {%d, %d, %d, %d}, .anchor = {%d, %d}},\n"
	sprite_defs:write(template:format(r.name, r.atlas, r.x, 255 - y1, x1, 255 - r.y, ax, r.h - ay))
end

-- local anim_counts = {}
-- local prev_index = 0

-- for name, count in pairs(anim_counts) do
-- 	sprite_enums:write(string.format("_DRIFT_SPRITE_%s_COUNT = %d,\n", name:upper(), count + 1))
-- end

local atlas_names = {}
local atlas_enums = io.open("atlas_enums.inc", "w")
local atlas_defs = io.open("atlas_defs.inc", "w")
for _, r in ipairs(rects) do
	local name = r.atlas
	if r.image_idx and not atlas_names[name] then
		atlas_names[name] = name
		atlas_enums:write(string.format("DRIFT_%s,\nDRIFT_%s_FX,", name, name))
		atlas_defs:write(string.format("[DRIFT_%s] = {LoadTexture, \"gfx/%s.qoi\"},", name, name))
		atlas_defs:write(string.format("[DRIFT_%s_FX] = {LoadTexture, \"gfx/%s_FX.qoi\"},", name, name))
	end
end

end)

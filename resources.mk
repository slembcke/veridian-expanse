.SECONDEXPANSION:

ATLAS = \
	resources/gfx/ATLAS_INPUT.ase \
	resources/gfx/ATLAS_LIGHTS.ase \
	resources/gfx/ATLAS_MISC.ase \
	resources/gfx/ATLAS_MISC_FX.ase \
	resources/gfx/ATLAS_UI.ase \
	resources/gfx/ATLAS_BG_LIGHT.ase \
	resources/gfx/ATLAS_BG_LIGHT_FX.ase \
	resources/gfx/ATLAS_BG_RADIO.ase \
	resources/gfx/ATLAS_BG_RADIO_FX.ase \
	resources/gfx/ATLAS_BG_CRYO.ase \
	resources/gfx/ATLAS_BG_CRYO_FX.ase \
	resources/gfx/ATLAS_BG_DARK.ase \
	resources/gfx/ATLAS_BG_DARK_FX.ase \

ART_ASE = \
	$(subst $(VPATH),resources,$(wildcard $(VPATH)/art/*.ase)) \

SHADER_INCLUDES = \
	shaders/drift_common.hlsl \
	shaders/lighting.hlsl \

SHADERS = \
	resources/shaders/primitive_overlay.hlsl \
	resources/shaders/primitive_linear.hlsl \
	resources/shaders/plasma.hlsl \
	resources/shaders/terrain.hlsl \
	resources/shaders/terrain_map.hlsl \
	resources/shaders/sprite.hlsl \
	resources/shaders/sprite_flash.hlsl \
	resources/shaders/sprite_overlay.hlsl \
	resources/shaders/light0.hlsl \
	resources/shaders/light1.hlsl \
	resources/shaders/light_blit0.hlsl \
	resources/shaders/light_blit1.hlsl \
	resources/shaders/shadow0.hlsl \
	resources/shaders/shadow1.hlsl \
	resources/shaders/shadow_mask0.hlsl \
	resources/shaders/shadow_mask1.hlsl \
	resources/shaders/nuklear.hlsl \
	resources/shaders/debug_lightfield.hlsl \
	resources/shaders/debug_terrain.hlsl \
	resources/shaders/debug_delta.hlsl \
	resources/shaders/resolve.hlsl \
	resources/shaders/image_blit.hlsl \
	resources/shaders/pause_blit.hlsl \
	resources/shaders/map_blit.hlsl \
	resources/shaders/present.hlsl \

SFX = $(subst $(VPATH),resources,$(wildcard $(VPATH)/sfx/*.ogg))
SCANS = $(subst $(VPATH),resources,$(wildcard $(VPATH)/gfx/scans/*.png))
IMAGES =  $(subst $(VPATH),resources,$(wildcard $(VPATH)/gfx/images/*.png))

FILES = \
	$(SCANS:.png=.qoi) $(IMAGES:.png=.qoi) \
	$(subst $(VPATH),resources,$(wildcard $(VPATH)/bin/*.bin)) \
	$(subst $(VPATH),resources,$(wildcard $(VPATH)/bin/*.txt)) \
	$(SFX) \
	$(subst $(VPATH),resources,$(wildcard $(VPATH)/music/*.ogg)) \
	$(SHADERS:%.hlsl=%.vert.spv) $(SHADERS:%.hlsl=%.frag.spv) \
	$(SHADERS:%.hlsl=%.vert) $(SHADERS:%.hlsl=%.frag) \
	resources/gfx/cursor.qoi \

drift-game-assets: resources.zip sound_inc strings_enums.inc
.PHONY: drift-game-assets

clean:
	-rm -r *.o resources resources.zip qoiconv packer atlas
.PHONY: clean

resources.zip: $(FILES) $(ATLAS:%.ase=%.qoi) atlas
	cd resources && zip -qr $(ZIP_FLAGS) ../resources.zip $(FILES:resources/%=%) gfx/*.qoi

%/.dir:
	"$(CMAKE_COMMAND)" -E make_directory $(@D)
	"$(CMAKE_COMMAND)" -E touch $@
.PRECIOUS: %/.dir

resources/gfx/scans%.qoi: gfx/scans%.png qoiconv $$(@D)/.dir
	pngquant --quality 80 --nofs -f --output $(@:.qoi=.png) $<
	./qoiconv $(@:.qoi=.png) $@

resources/gfx/images%.qoi: gfx/images%.png qoiconv $$(@D)/.dir
	# pngquant -f --output $(@:.qoi=.png) $<
	# ./qoiconv $(@:.qoi=.png) $@
	./qoiconv $< $@

resources/%: % $$(@D)/.dir
	"$(CMAKE_COMMAND)" -E copy $< $@

ART_OPTS = -b --inner-padding 1
resources/art/%.png resources/art/%.json: art/%.ase $$(@D)/.dir
	"$(ASEPRITE)" $(ART_OPTS) $< --sheet $(@D)/$(<F:.ase=.png) --data $(@D)/$(<F:.ase=.json) --format json-array --list-slices
	"$(ASEPRITE)" $(ART_OPTS) --layer "glow" $< --sheet $(@D)/$(<F:.ase=_e.png) > $(NULL)

resources/gfx/%.png resources/gfx/%.json: gfx/%.ase $$(@D)/.dir
	"$(ASEPRITE)" $(ART_OPTS) $< --save-as $(@D)/$(<F:.ase=.png) --data $(@D)/$(<F:.ase=.json) --format json-array --list-slices
	"$(ASEPRITE)" $(ART_OPTS) --layer "glow" $< --sheet $(@D)/$(<F:.ase=_e.png) > $(NULL)

resources/%.json: resources/%.png

onelua.o: ext/lua/onelua.c
	gcc -DMAKE_LIB -O -c $< -o $@

packer: tools/packer.c onelua.o
# The -Wl... bit is to force the removal of the .exe suffix on Windows.
	gcc -std=c99 -O0 -I $(VPATH)/ext $^ -Wl,-o$@ -lm
	
%.lua: tools/%.lua
	"$(CMAKE_COMMAND)" -E copy $< $@

# GDB = gdb -q --ex run --args
atlas: packer packer.lua json.lua $(ATLAS:.ase=.json) $(ART_ASE:.ase=.json) $(ART_ASE:.ase=_n.png)
	./packer $(ATLAS) $(ART_ASE)
	"$(CMAKE_COMMAND)" -E touch $@
	"$(CMAKE_COMMAND)" -E copy_if_different _sprite_enums.inc sprite_enums.inc
	"$(CMAKE_COMMAND)" -E copy_if_different _sprite_defs.inc sprite_defs.inc
	"$(CMAKE_COMMAND)" -E copy_if_different _atlas_enums.inc atlas_enums.inc
	"$(CMAKE_COMMAND)" -E copy_if_different _atlas_defs.inc atlas_defs.inc

qoiconv: ext/qoi/qoiconv.c
	gcc -std=c99 -O -I $(VPATH)/ext/stb $< -Wl,-o$@

%.qoi: %.png qoiconv
	./qoiconv $< $@

lua: ext/lua/onelua.c
	gcc -DMAKE_LUA -O $< -Wl,-o$@ -lm

sound_inc: lua sounds.lua $(SFX)
	./lua sounds.lua $(SFX)
	"$(CMAKE_COMMAND)" -E touch $@

resources/%.vert.spv: %.hlsl $(SHADER_INCLUDES) $$(@D)/.dir
	"$(GLSLANG)" -D -V -S vert -e VShader -o $@ $<
	"$(SPIRV_OPT)" -Oconfig="$(VPATH)/shaders/spirv-opt-gl.cfg" $@ -o $@

resources/%.frag.spv: %.hlsl $(SHADER_INCLUDES) $$(@D)/.dir
	"$(GLSLANG)" -D -V -S frag -e FShader -o $@ $<
	"$(SPIRV_OPT)" -Oconfig="$(VPATH)/shaders/spirv-opt-gl.cfg" $@ -o $@

%.vert: SPV=$(@:.vert=-gl.vert.spv)
resources/%.vert $(SPV): %.hlsl $(SHADER_INCLUDES) $$(@D)/.dir
	"$(GLSLANG)" -D -V -S vert -e VShader -o $(SPV) $< > $(NULL)
	"$(SPIRV_OPT)" -Oconfig="$(VPATH)/shaders/spirv-opt-gl.cfg" $(SPV) -o $(SPV)
	"$(SPIRV_CROSS)" --version 330 --no-420pack-extension --output $@ $(SPV)

%.frag: SPV=$(@:.frag=-gl.frag.spv)
resources/%.frag $(SPV): %.hlsl $(SHADER_INCLUDES) $$(@D)/.dir
	"$(GLSLANG)" -D -V -S frag -e FShader -o $(SPV) $< > $(NULL)
	"$(SPIRV_OPT)" -Oconfig="$(VPATH)/shaders/spirv-opt-gl.cfg" $(SPV) -o $(SPV)
	"$(SPIRV_CROSS)" --version 330 --no-420pack-extension --output $@ $(SPV)

strings_enums.inc: $(VPATH)/src/drift_strings_en.c lua
	./lua $(VPATH)/tools/strings.lua $< $@

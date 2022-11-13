#pragma once

#define DRIFT_GFX_VERTEX_ATTRIB_COUNT 8
#define DRIFT_GFX_UNIFORM_BINDING_COUNT (4u)
#define DRIFT_GFX_SAMPLER_BINDING_COUNT (4u)
#define DRIFT_GFX_TEXTURE_BINDING_COUNT (8u)
#define DRIFT_GFX_RENDER_TARGET_COUNT 6

typedef enum {
	_DRIFT_GFX_TYPE_NONE,
	DRIFT_GFX_TYPE_U8,
	DRIFT_GFX_TYPE_U8_2,
	DRIFT_GFX_TYPE_U8_4,
	DRIFT_GFX_TYPE_U16,
	DRIFT_GFX_TYPE_UNORM8_2,
	DRIFT_GFX_TYPE_UNORM8_4,
	DRIFT_GFX_TYPE_FLOAT32,
	DRIFT_GFX_TYPE_FLOAT32_2,
	DRIFT_GFX_TYPE_FLOAT32_3,
	DRIFT_GFX_TYPE_FLOAT32_4,
	_DRIFT_GFX_TYPE_COUNT,
} DriftGfxType;

typedef enum {
	DRIFT_GFX_BLEND_FACTOR_ZERO,
	DRIFT_GFX_BLEND_FACTOR_ONE,
	DRIFT_GFX_BLEND_FACTOR_SRC_COLOR,
	DRIFT_GFX_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
	DRIFT_GFX_BLEND_FACTOR_DST_COLOR,
	DRIFT_GFX_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
	DRIFT_GFX_BLEND_FACTOR_SRC_ALPHA,
	DRIFT_GFX_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
	DRIFT_GFX_BLEND_FACTOR_DST_ALPHA,
	DRIFT_GFX_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
	DRIFT_GFX_BLEND_FACTOR_SRC_ALPHA_SATURATE,
	DRIFT_GFX_BLEND_FACTOR_CONSTANT_COLOR,
	_DRIFT_GFX_BLEND_FACTOR_COUNT,
} DriftGfxBlendFactor;

typedef enum {
	DRIFT_GFX_BLEND_OP_ADD,
	DRIFT_GFX_BLEND_OP_SUBTRACT,
	DRIFT_GFX_BLEND_OP_REVERSE_SUBTRACT,
	DRIFT_GFX_BLEND_OP_MIN,
	DRIFT_GFX_BLEND_OP_MAX,
	_DRIFT_GFX_BLEND_OP_COUNT,
} DriftGfxBlendOp;

typedef enum {
	DRIFT_GFX_TEXTURE_2D,
	DRIFT_GFX_TEXTURE_2D_ARRAY,
	_DRIFT_GFX_TEXTURE_COUNT,
} DriftGfxTextureType;

typedef enum {
	DRIFT_GFX_TEXTURE_FORMAT_RGBA8,
	DRIFT_GFX_TEXTURE_FORMAT_RGBA16F,
	_DRIFT_GFX_TEXTURE_FORMAT_COUNT,
} DriftGfxTextureFormat;

typedef enum {
	DRIFT_GFX_FILTER_NEAREST,
	DRIFT_GFX_FILTER_LINEAR,
	_DRIFT_GFX_FILTER_COUNT,
} DriftGfxFilter;

typedef enum {
	DRIFT_GFX_MIP_FILTER_NONE,
	DRIFT_GFX_MIP_FILTER_NEAREST,
	DRIFT_GFX_MIP_FILTER_LINEAR,
	_DRIFT_GFX_MIP_FILTER_COUNT,
} DriftGfxMipFilter;

typedef enum {
	DRIFT_GFX_ADDRESS_MODE_CLAMP_TO_EDGE,
	DRIFT_GFX_ADDRESS_MODE_CLAMP_TO_BORDER,
	DRIFT_GFX_ADDRESS_MODE_REPEAT,
	DRIFT_GFX_ADDRESS_MODE_MIRRORED_REPEAT,
	_DRIFT_GFX_ADDRESS_MODE_COUNT,
} DriftGfxAddressMode;

typedef enum {
	DRIFT_GFX_CULL_MODE_NONE,
	DRIFT_GFX_CULL_MODE_FRONT,
	DRIFT_GFX_CULL_MODE_BACK,
	_DRIFT_GFX_CULL_MODE_COUNT,
} DriftGfxCullMode;

typedef enum {
	DRIFT_GFX_LOAD_ACTION_DONT_CARE,
	DRIFT_GFX_LOAD_ACTION_CLEAR,
	DRIFT_GFX_LOAD_ACTION_LOAD,
	_DRIFT_GFX_LOAD_ACTION_COUNT,
} DriftGfxLoadAction;

typedef enum {
	DRIFT_GFX_STORE_ACTION_DONT_CARE,
	DRIFT_GFX_STORE_ACTION_STORE,
	_DRIFT_GFX_STORE_ACTION_COUNT,
} DriftGfxStoreAction;

typedef struct DriftGfxSampler DriftGfxSampler;
typedef struct DriftGfxTexture DriftGfxTexture;
typedef struct DriftGfxRenderTarget DriftGfxRenderTarget;
typedef struct DriftGfxShader DriftGfxShader;
typedef struct DriftGfxPipeline DriftGfxPipeline;

typedef struct DriftGfxDriver DriftGfxDriver;
typedef struct DriftGfxRenderer DriftGfxRenderer;

typedef struct {
	DriftGfxFilter min_filter, mag_filter;
	DriftGfxMipFilter mip_filter;
	DriftGfxAddressMode address_x, address_y;
} DriftGfxSamplerOptions;

typedef struct {
	const char* name;
	DriftGfxTextureType type;
	DriftGfxTextureFormat format;
	uint layers;
	bool render_target;
} DriftGfxTextureOptions;

typedef struct {
	DriftGfxType type;
	u32 offset;
	bool instanced;
} DriftGfxVertexAttrib;

typedef struct {
	DriftGfxVertexAttrib vertex[DRIFT_GFX_VERTEX_ATTRIB_COUNT];
	size_t vertex_stride, instance_stride;
	const char* uniform[DRIFT_GFX_UNIFORM_BINDING_COUNT];
	const char* sampler[DRIFT_GFX_SAMPLER_BINDING_COUNT];
	const char* texture[DRIFT_GFX_TEXTURE_BINDING_COUNT];
} DriftGfxShaderDesc;

typedef struct DriftGfxBlendMode {
	DriftGfxBlendOp color_op, alpha_op;
	DriftGfxBlendFactor color_src_factor, color_dst_factor;
	DriftGfxBlendFactor alpha_src_factor, alpha_dst_factor;
	bool enable_blend_color;
} DriftGfxBlendMode;

extern DriftGfxBlendMode DriftGfxBlendModeAlpha;
extern DriftGfxBlendMode DriftGfxBlendModePremultipliedAlpha;
extern DriftGfxBlendMode DriftGfxBlendModeAdd;
extern DriftGfxBlendMode DriftGfxBlendModeMultiply;

typedef struct {
	DriftGfxCullMode cull_mode;
	const DriftGfxShader* shader;
	const DriftGfxBlendMode* blend;
	const DriftGfxRenderTarget* target;
} DriftGfxPipelineOptions;

typedef struct {
	u32 offset, size;
} DriftGfxBufferBinding;

typedef struct {
	DriftGfxBufferBinding vertex;
	DriftGfxBufferBinding instance;
	DriftGfxBufferBinding uniforms[DRIFT_GFX_UNIFORM_BINDING_COUNT];
	DriftGfxSampler const* samplers[DRIFT_GFX_SAMPLER_BINDING_COUNT];
	DriftGfxTexture const* textures[DRIFT_GFX_TEXTURE_BINDING_COUNT];
	DriftVec4 blend_color;
} DriftGfxPipelineBindings;

typedef struct {
	const char* name;
	DriftGfxLoadAction load;
	DriftGfxStoreAction store;
	
	struct {
		DriftGfxTexture* texture;
		uint layer;
	} bindings[DRIFT_GFX_RENDER_TARGET_COUNT];
} DriftGfxRenderTargetOptions;

typedef struct {
	void* ptr;
	DriftGfxBufferBinding binding;
} DriftGfxBufferSlice;

DriftVec2 DriftGfxRendererDefaultExtent(DriftGfxRenderer* renderer);

DriftGfxBufferSlice DriftGfxRendererPushGeometry(DriftGfxRenderer* renderer, const void* ptr, size_t length);
DriftGfxBufferSlice DriftGfxRendererPushIndexes(DriftGfxRenderer* renderer, const void* ptr, size_t length);
DriftGfxBufferSlice DriftGfxRendererPushUniforms(DriftGfxRenderer* renderer, const void* ptr, size_t size);

void DriftGfxRendererPushBindTargetCommand(DriftGfxRenderer* renderer, 	DriftGfxRenderTarget* rt, DriftVec4 clear_color);
// Push DRIFT_AABB2_ALL to disable scissor.
void DriftGfxRendererPushScissorCommand(DriftGfxRenderer* renderer, DriftAABB2 bounds);

DriftGfxPipelineBindings* DriftGfxRendererPushBindPipelineCommand(DriftGfxRenderer* renderer, DriftGfxPipeline* pipeline);
void DriftGfxRendererPushDrawIndexedCommand(DriftGfxRenderer* renderer, DriftGfxBufferBinding index_binding, u32 index_count, u32 instance_count);

struct DriftGfxDriver {
	void* ctx;
	DriftGfxShader* (*load_shader)(const DriftGfxDriver* driver, const char* name, const DriftGfxShaderDesc* desc);
	DriftGfxPipeline* (*new_pipeline)(const DriftGfxDriver* driver, DriftGfxPipelineOptions options);
	DriftGfxSampler* (*new_sampler)(const DriftGfxDriver* driver, DriftGfxSamplerOptions options);
	DriftGfxTexture* (*new_texture)(const DriftGfxDriver* driver, uint width, uint height, DriftGfxTextureOptions options);
	DriftGfxRenderTarget* (*new_target)(const DriftGfxDriver* driver, DriftGfxRenderTargetOptions options);
	void (*load_texture_layer)(const DriftGfxDriver* driver, DriftGfxTexture* texture, uint layer, const void* pixels);
	void (*free_objects)(const DriftGfxDriver* driver, void* obj[], uint count);
	void (*free_all)(const DriftGfxDriver* driver);
};

extern const DriftGfxDriver DriftGfxDriverGL;
extern const DriftGfxDriver DriftGfxDriverVk;

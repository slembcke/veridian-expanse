#define DRIFT_GFX_VERTEX_BUFFER_SIZE (6 << 20)
#define DRIFT_GFX_INDEX_BUFFER_SIZE (1 << 20)
#define DRIFT_GFX_UNIFORM_BUFFER_SIZE (1 << 20)

typedef void DriftGfxDestructor(const DriftGfxDriver* driver, void* obj);
void DriftGfxFreeObjects(const DriftGfxDriver* driver, DriftMap* destructors, void* objects[], uint count);
void DriftGfxFreeAll(const DriftGfxDriver* driver, DriftMap* destructors);

typedef struct DriftGfxSampler {
} DriftGfxSampler;

struct DriftGfxTexture {
	DriftGfxTextureOptions options;
	uint width, height;
};

typedef struct DriftGfxRenderTarget {
	DriftGfxLoadAction load;
	DriftGfxStoreAction store;
	DriftVec2 framebuffer_size;
} DriftGfxRenderTarget;

typedef struct DriftGfxShader {
	const char* name;
	const DriftGfxShaderDesc* desc;
} DriftGfxShader;

struct DriftGfxPipeline {
	DriftGfxPipelineOptions options;
};

typedef struct {
	const DriftGfxPipeline* pipeline;
	const DriftGfxRenderTarget* target;
	DriftVec2 extent;
} DriftGfxRenderState;

typedef struct DriftGfxCommand DriftGfxCommand;

typedef	void DriftGfxCommandFunc(
	const DriftGfxRenderer* renderer,
	const DriftGfxCommand* command,
	DriftGfxRenderState* state
);

struct DriftGfxCommand {
	DriftGfxCommandFunc* func;
	DriftGfxCommand* next;
};

typedef struct {
	DriftGfxCommand base;
	const DriftGfxRenderTarget* rt;
	DriftVec4 clear_color;
} DriftGfxCommandTarget;

typedef struct {
	DriftGfxCommand base;
	DriftAABB2 bounds;
} DriftGfxCommandScissor;

typedef struct {
	DriftGfxCommand base;
	const DriftGfxPipeline* pipeline;
	const DriftGfxPipelineBindings* bindings;
} DriftGfxCommandPipeline;

typedef struct {
	DriftGfxCommand base;
	DriftGfxBufferBinding index_binding;
	u32 index_count, instance_count;
} DriftGfxCommandDraw;

typedef struct {
	DriftGfxCommandFunc* bind_target;
	DriftGfxCommandFunc* set_scissor;
	DriftGfxCommandFunc* bind_pipeline;
	DriftGfxCommandFunc* draw_indexed;
} DriftGfxVTable;

typedef struct {
	void* vertex;
	void* index;
	void* uniform;
} DriftGfxBufferPointers;

struct DriftGfxRenderer {
	DriftGfxVTable vtable;
	DriftZoneMem* mem;
	
	DriftVec2 default_extent;
	
	DriftGfxCommand* first_command;
	DriftGfxCommand** command_cursor;
	
	size_t uniform_alignment;
	DriftGfxBufferPointers ptr, cursor;
	
	u8 temp_buffer[64*1024];
};

void DriftGfxRendererInit(DriftGfxRenderer* renderer, DriftGfxVTable vtable);
void DriftRendererExecuteCommands(DriftGfxRenderer* renderer);
void DriftGfxRendererPrepare(DriftGfxRenderer* renderer, DriftVec2 default_framebuffer_size, DriftZoneMem* mem);

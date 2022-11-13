#include <stdio.h>
#include <string.h>

#if __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/glcorearb.h>
#endif

#include <SDL.h>
#include "tina/tina_jobs.h"
#include "tracy/TracyC.h"

#include "drift_types.h"
#include "drift_math.h"
#include "drift_util.h"
#include "drift_mem.h"
#include "drift_table.h"
#include "drift_map.h"
#include "drift_gfx.h"
#include "drift_gfx_internal.h"
#include "drift_app.h"

typedef struct {
	DriftGfxTexture base;
	GLuint id;
} DriftGLTexture;

typedef struct {
	DriftGfxSampler base;
	GLuint id;
} DriftGLSampler;

typedef struct {
	DriftGfxShader base;
	GLuint id;
	
	// Which Drift resource (value) to map to a GL binding (index).
	struct {int texture, sampler;} combined_mapping[DRIFT_GFX_TEXTURE_BINDING_COUNT];
} DriftGLShader;

typedef struct {
	DriftGfxRenderTarget base;
	GLuint id;
} DriftGLRenderTarget;

typedef struct {
	DriftGfxRenderer base;
	GLuint vertex_buffer, index_buffer, uniform_buffer;
	GLsync fence;
} DriftGLRenderer;

#define DRIFT_GL_RENDERER_COUNT 2

typedef struct {
	SDL_GLContext* gl_context;
	SDL_GLContext* sync_context;
	
	DriftMap destructors;
	
	DriftGLRenderer* renderers[DRIFT_GL_RENDERER_COUNT];
	uint renderer_index;
} DriftSDLGLContext;

static PFNGLGETERRORPROC _glGetError;
static PFNGLCREATESHADERPROC _glCreateShader;
static PFNGLSHADERSOURCEPROC _glShaderSource;
static PFNGLCOMPILESHADERPROC _glCompileShader;
static PFNGLGETSHADERIVPROC _glGetShaderiv;
static PFNGLGETSHADERINFOLOGPROC _glGetShaderInfoLog;
static PFNGLDELETESHADERPROC _glDeleteShader;
static PFNGLCREATEPROGRAMPROC _glCreateProgram;
static PFNGLATTACHSHADERPROC _glAttachShader;
static PFNGLLINKPROGRAMPROC _glLinkProgram;
static PFNGLGETPROGRAMIVPROC _glGetProgramiv;
static PFNGLGETPROGRAMINFOLOGPROC _glGetProgramInfoLog;
static PFNGLDELETEPROGRAMPROC _glDeleteProgram;
static PFNGLDELETESHADERPROC _glDeleteShader;
static PFNGLMAPBUFFERRANGEPROC _glMapBufferRange;
static PFNGLBINDBUFFERPROC _glBindBuffer;
static PFNGLBINDVERTEXARRAYPROC _glBindVertexArray;
static PFNGLFLUSHMAPPEDBUFFERRANGEPROC _glFlushMappedBufferRange;
static PFNGLUNMAPBUFFERPROC _glUnmapBuffer;
static PFNGLFENCESYNCPROC _glFenceSync;
static PFNGLCLIENTWAITSYNCPROC _glClientWaitSync;
static PFNGLDELETESYNCPROC _glDeleteSync;
static PFNGLBINDFRAMEBUFFERPROC _glBindFramebuffer;
static PFNGLVIEWPORTPROC _glViewport;
static PFNGLCLEARCOLORPROC _glClearColor;
static PFNGLCLEARPROC _glClear;
static PFNGLUSEPROGRAMPROC _glUseProgram;
static PFNGLENABLEPROC _glEnable;
static PFNGLDISABLEPROC _glDisable;
static PFNGLSCISSORPROC _glScissor;
static PFNGLBLENDEQUATIONSEPARATEPROC _glBlendEquationSeparate;
static PFNGLBLENDFUNCSEPARATEPROC _glBlendFuncSeparate;
static PFNGLBLENDCOLORPROC _glBlendColor;
static PFNGLCULLFACEPROC _glCullFace;
static PFNGLENABLEVERTEXATTRIBARRAYPROC _glEnableVertexAttribArray;
static PFNGLDISABLEVERTEXATTRIBARRAYPROC _glDisableVertexAttribArray;
static PFNGLVERTEXATTRIBIPOINTERPROC _glVertexAttribIPointer;
static PFNGLVERTEXATTRIBPOINTERPROC _glVertexAttribPointer;
static PFNGLVERTEXATTRIBDIVISORPROC _glVertexAttribDivisor;
static PFNGLBINDBUFFERRANGEPROC _glBindBufferRange;
static PFNGLACTIVETEXTUREPROC _glActiveTexture;
static PFNGLBINDTEXTUREPROC _glBindTexture;
static PFNGLBINDSAMPLERPROC _glBindSampler;
static PFNGLDRAWELEMENTSINSTANCEDPROC _glDrawElementsInstanced;
static PFNGLGETINTEGERVPROC _glGetIntegerv;
static PFNGLGETSTRINGPROC _glGetString;
static PFNGLGENVERTEXARRAYSPROC _glGenVertexArrays;
static PFNGLGENBUFFERSPROC _glGenBuffers;
static PFNGLBUFFERDATAPROC _glBufferData;
static PFNGLGETACTIVEUNIFORMBLOCKNAMEPROC _glGetActiveUniformBlockName;
static PFNGLUNIFORMBLOCKBINDINGPROC _glUniformBlockBinding;
static PFNGLGETUNIFORMBLOCKINDEXPROC _glGetUniformBlockIndex;
static PFNGLGETACTIVEUNIFORMPROC _glGetActiveUniform;
static PFNGLGETUNIFORMLOCATIONPROC _glGetUniformLocation;
static PFNGLUNIFORM1IPROC _glUniform1i;
static PFNGLGENSAMPLERSPROC _glGenSamplers;
static PFNGLDELETESAMPLERSPROC _glDeleteSamplers;
static PFNGLSAMPLERPARAMETERIPROC _glSamplerParameteri;
static PFNGLGENTEXTURESPROC _glGenTextures;
static PFNGLDELETETEXTURESPROC _glDeleteTextures;
static PFNGLTEXIMAGE2DPROC _glTexImage2D;
static PFNGLTEXIMAGE3DPROC _glTexImage3D;
static PFNGLTEXPARAMETERIPROC _glTexParameteri;
static PFNGLTEXSUBIMAGE2DPROC _glTexSubImage2D;
static PFNGLTEXSUBIMAGE3DPROC _glTexSubImage3D;
static PFNGLGENFRAMEBUFFERSPROC _glGenFramebuffers;
static PFNGLDELETEBUFFERSPROC _glDeleteFramebuffers;
static PFNGLBINDFRAMEBUFFERPROC _glBindFramebuffer;
static PFNGLFRAMEBUFFERTEXTURE2DPROC _glFramebufferTexture2D;
static PFNGLFRAMEBUFFERTEXTURELAYERPROC _glFramebufferTextureLayer;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC _glCheckFramebufferStatus;
static PFNGLDRAWBUFFERSPROC _glDrawBuffers;

#define DRIFT_GL_LOAD_FUNC(name) {_##name = SDL_GL_GetProcAddress(DRIFT_STR(name)); DRIFT_ASSERT_WARN(_##name, "Failed to load OpenGL function.");}

static void DriftLoadGL(void){
	DRIFT_GL_LOAD_FUNC(glGetError);
	DRIFT_GL_LOAD_FUNC(glCreateShader);
	DRIFT_GL_LOAD_FUNC(glShaderSource);
	DRIFT_GL_LOAD_FUNC(glCompileShader);
	DRIFT_GL_LOAD_FUNC(glGetShaderiv);
	DRIFT_GL_LOAD_FUNC(glGetShaderInfoLog);
	DRIFT_GL_LOAD_FUNC(glDeleteShader);
	DRIFT_GL_LOAD_FUNC(glCreateProgram);
	DRIFT_GL_LOAD_FUNC(glAttachShader);
	DRIFT_GL_LOAD_FUNC(glLinkProgram);
	DRIFT_GL_LOAD_FUNC(glGetProgramiv);
	DRIFT_GL_LOAD_FUNC(glGetProgramInfoLog);
	DRIFT_GL_LOAD_FUNC(glDeleteProgram);
	DRIFT_GL_LOAD_FUNC(glDeleteShader);
	DRIFT_GL_LOAD_FUNC(glMapBufferRange);
	DRIFT_GL_LOAD_FUNC(glBindBuffer);
	DRIFT_GL_LOAD_FUNC(glBindVertexArray);
	DRIFT_GL_LOAD_FUNC(glFlushMappedBufferRange);
	DRIFT_GL_LOAD_FUNC(glUnmapBuffer);
	DRIFT_GL_LOAD_FUNC(glFenceSync);
	DRIFT_GL_LOAD_FUNC(glClientWaitSync);
	DRIFT_GL_LOAD_FUNC(glDeleteSync);
	DRIFT_GL_LOAD_FUNC(glBindFramebuffer);
	DRIFT_GL_LOAD_FUNC(glViewport);
	DRIFT_GL_LOAD_FUNC(glClearColor);
	DRIFT_GL_LOAD_FUNC(glClear);
	DRIFT_GL_LOAD_FUNC(glUseProgram);
	DRIFT_GL_LOAD_FUNC(glEnable);
	DRIFT_GL_LOAD_FUNC(glDisable);
	DRIFT_GL_LOAD_FUNC(glScissor);
	DRIFT_GL_LOAD_FUNC(glBlendEquationSeparate);
	DRIFT_GL_LOAD_FUNC(glBlendFuncSeparate);
	DRIFT_GL_LOAD_FUNC(glBlendColor);
	DRIFT_GL_LOAD_FUNC(glCullFace);
	DRIFT_GL_LOAD_FUNC(glEnableVertexAttribArray);
	DRIFT_GL_LOAD_FUNC(glDisableVertexAttribArray);
	DRIFT_GL_LOAD_FUNC(glVertexAttribIPointer);
	DRIFT_GL_LOAD_FUNC(glVertexAttribPointer);
	DRIFT_GL_LOAD_FUNC(glVertexAttribDivisor);
	DRIFT_GL_LOAD_FUNC(glBindBufferRange);
	DRIFT_GL_LOAD_FUNC(glActiveTexture);
	DRIFT_GL_LOAD_FUNC(glBindTexture);
	DRIFT_GL_LOAD_FUNC(glBindSampler);
	DRIFT_GL_LOAD_FUNC(glDrawElementsInstanced);
	DRIFT_GL_LOAD_FUNC(glGetIntegerv);
	DRIFT_GL_LOAD_FUNC(glGetString);
	DRIFT_GL_LOAD_FUNC(glGenVertexArrays);
	DRIFT_GL_LOAD_FUNC(glGenBuffers);
	DRIFT_GL_LOAD_FUNC(glBufferData);
	DRIFT_GL_LOAD_FUNC(glGetActiveUniformBlockName);
	DRIFT_GL_LOAD_FUNC(glUniformBlockBinding);
	DRIFT_GL_LOAD_FUNC(glGetUniformBlockIndex);
	DRIFT_GL_LOAD_FUNC(glGetActiveUniform);
	DRIFT_GL_LOAD_FUNC(glGetUniformLocation);
	DRIFT_GL_LOAD_FUNC(glUniform1i);
	DRIFT_GL_LOAD_FUNC(glGenSamplers);
	DRIFT_GL_LOAD_FUNC(glDeleteSamplers);
	DRIFT_GL_LOAD_FUNC(glSamplerParameteri);
	DRIFT_GL_LOAD_FUNC(glGenTextures);
	DRIFT_GL_LOAD_FUNC(glDeleteTextures);
	DRIFT_GL_LOAD_FUNC(glTexImage2D);
	DRIFT_GL_LOAD_FUNC(glTexImage3D);
	DRIFT_GL_LOAD_FUNC(glTexParameteri);
	DRIFT_GL_LOAD_FUNC(glTexSubImage2D);
	DRIFT_GL_LOAD_FUNC(glTexSubImage3D);
	DRIFT_GL_LOAD_FUNC(glGenFramebuffers);
	DRIFT_GL_LOAD_FUNC(glDeleteFramebuffers);
	DRIFT_GL_LOAD_FUNC(glBindFramebuffer);
	DRIFT_GL_LOAD_FUNC(glFramebufferTexture2D);
	DRIFT_GL_LOAD_FUNC(glFramebufferTextureLayer);
	DRIFT_GL_LOAD_FUNC(glCheckFramebufferStatus);
	DRIFT_GL_LOAD_FUNC(glDrawBuffers);
}

#define DRIFTGL_ASSERT_ERRORS() _DriftGLAssertErrors(__FILE__, __LINE__)
static void _DriftGLAssertErrors(const char *file, int line){
	for(GLenum err; (err = _glGetError());){
		const char *error = "Unknown Error";
		
		switch(err){
			case 0x0500: error = "Invalid Enumeration"; break;
			case 0x0501: error = "Invalid Value"; break;
			case 0x0502: error = "Invalid Operation"; break;
			default: error = "Unknown error"; break;
		}
		
		DRIFT_ABORT("GL Error(%s:%d): %s", file, line, error);
	}
}

typedef void ShaderParamFunc(GLuint shader, GLenum pname, GLint* param);
typedef void ShaderLogFunc(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);

static bool DriftGLCheckShaderError(GLuint obj, GLenum status, ShaderParamFunc param_func, ShaderLogFunc log_func){
	GLint success;
	param_func(obj, status, &success);
	
	if(!success){
		GLint length;
		param_func(obj, GL_INFO_LOG_LENGTH, &length);

		char log[length];
		log_func(obj, length, NULL, log);
		// DRIFTGL_ASSERT_ERRORS();
		DRIFT_LOG("Shader compile error for 0x%04X: %s", status, log);
		return false;
	} else {
		return true;
	}
}

static void DriftGLLogShader(const char *label, const DriftData source){
	DRIFT_LOG("%s", label);
	
	const char *cursor = source.ptr;
	for(int line = 1; true; line++){
		fprintf(stderr, "% 4d: ", line);
		while(true){
			if(cursor == source.ptr + source.size) return;
			fputc(cursor[0], stderr);
			cursor++;
			
			if(cursor[-1] == '\n') break;
		}
	}
}

static GLuint DriftGLCompileShaderSource(GLenum type, const DriftData source){
	GLuint shader = _glCreateShader(type);
	_glShaderSource(shader, 1, (const GLchar *[]){source.ptr}, (GLint[]){source.size});
	_glCompileShader(shader);
	DRIFTGL_ASSERT_ERRORS();
	
	if(DriftGLCheckShaderError(shader, GL_COMPILE_STATUS, _glGetShaderiv, _glGetShaderInfoLog)){
		return shader;
	} else {
		DriftGLLogShader((type == GL_VERTEX_SHADER) ? "Vertex Shader:" : "Fragment Shader:", source);
		_glDeleteShader(shader);
		DRIFTGL_ASSERT_ERRORS();
		
		return 0;
	}
}

static GLuint DriftGLCompileShader(const DriftData vsource, const DriftData fsource){
	GLuint shader = _glCreateProgram();
	DRIFTGL_ASSERT_ERRORS();
	
	GLint vshader = DriftGLCompileShaderSource(GL_VERTEX_SHADER, vsource);
	GLint fshader = DriftGLCompileShaderSource(GL_FRAGMENT_SHADER, fsource);

	if(!fshader || !vshader) goto cleanup;
	_glAttachShader(shader, vshader);
	_glAttachShader(shader, fshader);
	DRIFTGL_ASSERT_ERRORS();
	
	_glLinkProgram(shader);
	_glDeleteShader(vshader);
	_glDeleteShader(fshader);
	DRIFTGL_ASSERT_ERRORS();
	
	if(DriftGLCheckShaderError(shader, GL_LINK_STATUS, _glGetProgramiv, _glGetProgramInfoLog)){
		return shader;
	} else {
		if(vshader && fshader){
			DriftGLLogShader("Vertex Shader", vsource);
			DriftGLLogShader("Fragment Shader", fsource);
		}
		
		cleanup:
		_glDeleteProgram(shader);
		_glDeleteShader(vshader);
		_glDeleteShader(fshader);
		DRIFTGL_ASSERT_ERRORS();
		
		return 0;
	}
}

static const struct {
	GLenum type;
	GLint size;
	bool normalized;
	bool integer;
} DriftGfxTypeMap[_DRIFT_GFX_TYPE_COUNT] = {
	[DRIFT_GFX_TYPE_U8] = {GL_UNSIGNED_BYTE, 1, .integer = true},
	[DRIFT_GFX_TYPE_U16] = {GL_UNSIGNED_SHORT, 1, .integer = true},
	[DRIFT_GFX_TYPE_U8_2] = {GL_UNSIGNED_BYTE, 2, .integer = true},
	[DRIFT_GFX_TYPE_U8_4] = {GL_UNSIGNED_BYTE, 4, .integer = true},
	[DRIFT_GFX_TYPE_UNORM8_2] = {GL_UNSIGNED_BYTE, 2, .normalized = true},
	[DRIFT_GFX_TYPE_UNORM8_4] = {GL_UNSIGNED_BYTE, 4, .normalized = true},
	[DRIFT_GFX_TYPE_FLOAT32] = {GL_FLOAT, 1},
	[DRIFT_GFX_TYPE_FLOAT32_2] = {GL_FLOAT, 2},
	[DRIFT_GFX_TYPE_FLOAT32_3] = {GL_FLOAT, 3},
	[DRIFT_GFX_TYPE_FLOAT32_4] = {GL_FLOAT, 4},
};

static const GLenum DriftGfxBlendFactorToGL[] = {
	[DRIFT_GFX_BLEND_FACTOR_ZERO] = GL_ZERO,
	[DRIFT_GFX_BLEND_FACTOR_ONE] = GL_ONE,
	[DRIFT_GFX_BLEND_FACTOR_SRC_COLOR] = GL_SRC_COLOR,
	[DRIFT_GFX_BLEND_FACTOR_ONE_MINUS_SRC_COLOR] = GL_ONE_MINUS_SRC_COLOR,
	[DRIFT_GFX_BLEND_FACTOR_DST_COLOR] = GL_ONE_MINUS_DST_COLOR,
	[DRIFT_GFX_BLEND_FACTOR_ONE_MINUS_DST_COLOR] = GL_ONE_MINUS_DST_COLOR,
	[DRIFT_GFX_BLEND_FACTOR_SRC_ALPHA] = GL_SRC_ALPHA,
	[DRIFT_GFX_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA] = GL_ONE_MINUS_SRC_ALPHA,
	[DRIFT_GFX_BLEND_FACTOR_DST_ALPHA] = GL_DST_ALPHA,
	[DRIFT_GFX_BLEND_FACTOR_ONE_MINUS_DST_ALPHA] = GL_ONE_MINUS_DST_ALPHA,
	[DRIFT_GFX_BLEND_FACTOR_SRC_ALPHA_SATURATE] = GL_SRC_ALPHA_SATURATE,
	[DRIFT_GFX_BLEND_FACTOR_CONSTANT_COLOR] = GL_CONSTANT_COLOR,
};

static const GLenum DriftGfxBlendOpToGL[] = {
	[DRIFT_GFX_BLEND_OP_ADD] = GL_FUNC_ADD,
	[DRIFT_GFX_BLEND_OP_SUBTRACT] = GL_FUNC_SUBTRACT,
	[DRIFT_GFX_BLEND_OP_REVERSE_SUBTRACT] = GL_FUNC_REVERSE_SUBTRACT,
	[DRIFT_GFX_BLEND_OP_MIN] = GL_MIN,
	[DRIFT_GFX_BLEND_OP_MAX] = GL_MAX,
};

#define BUFFER_ACCESS_WRITE (GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_FLUSH_EXPLICIT_BIT)

static void DriftGLRendererMapAndUnbindBuffers(DriftGLRenderer* renderer){
	renderer->base.ptr.vertex = _glMapBufferRange(GL_ARRAY_BUFFER, 0, DRIFT_GFX_VERTEX_BUFFER_SIZE, BUFFER_ACCESS_WRITE);
	renderer->base.ptr.index = _glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER, 0, DRIFT_GFX_INDEX_BUFFER_SIZE, BUFFER_ACCESS_WRITE);
	renderer->base.ptr.uniform = _glMapBufferRange(GL_UNIFORM_BUFFER, 0, DRIFT_GFX_UNIFORM_BUFFER_SIZE, BUFFER_ACCESS_WRITE);
	DRIFTGL_ASSERT_ERRORS();
	
	_glBindBuffer(GL_ARRAY_BUFFER, 0);
	_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	_glBindBuffer(GL_UNIFORM_BUFFER, 0);
	DRIFTGL_ASSERT_ERRORS();
}

static void DriftGLBindBuffers(DriftGLRenderer *renderer, bool flush_and_unmap){
	DriftGLRenderer* _renderer = (DriftGLRenderer*)renderer;
	_glBindBuffer(GL_ARRAY_BUFFER, _renderer->vertex_buffer);
	_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _renderer->index_buffer);
	_glBindBuffer(GL_UNIFORM_BUFFER, _renderer->uniform_buffer);
	DRIFTGL_ASSERT_ERRORS();
	
	if(flush_and_unmap){
		_glFlushMappedBufferRange(GL_ARRAY_BUFFER, 0, DRIFT_GFX_VERTEX_BUFFER_SIZE);
		_glFlushMappedBufferRange(GL_ELEMENT_ARRAY_BUFFER, 0, DRIFT_GFX_INDEX_BUFFER_SIZE);
		_glFlushMappedBufferRange(GL_UNIFORM_BUFFER, 0, DRIFT_GFX_UNIFORM_BUFFER_SIZE);
		_glUnmapBuffer(GL_ARRAY_BUFFER);
		_glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
		_glUnmapBuffer(GL_UNIFORM_BUFFER);
		DRIFTGL_ASSERT_ERRORS();
	}
}

static void DriftGLRendererExecute(DriftGLRenderer* renderer){
	DriftGLBindBuffers(renderer, true);
	
	_glEnable(GL_SCISSOR_TEST);
	DriftRendererExecuteCommands(&renderer->base);
	((DriftGLRenderer*)renderer)->fence = _glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
	
	DriftGLRendererMapAndUnbindBuffers(renderer);
}

static void DriftGLRendererWait(DriftGLRenderer* renderer){
	if(renderer->fence){
		switch(_glClientWaitSync(renderer->fence, GL_SYNC_FLUSH_COMMANDS_BIT, UINT64_MAX)){
			case GL_ALREADY_SIGNALED:
			case GL_CONDITION_SATISFIED:
				// Fence has completed. Clean it up.
				_glDeleteSync(renderer->fence);
				renderer->fence = NULL;
				break;
			default:
				DRIFTGL_ASSERT_ERRORS();
				DRIFT_ASSERT_HARD(false, "Failed to wait for GL fence.");
		}
	}
}

static void DriftGLCommandBindTarget(const DriftGfxRenderer* renderer, const DriftGfxCommand* command, DriftGfxRenderState* state){
	const DriftGfxCommandTarget* _command = (DriftGfxCommandTarget*)command;
	
	DriftVec2 size = renderer->default_extent;
	DriftGLRenderTarget* rt = (DriftGLRenderTarget*)_command->rt;
	if(rt){
		_glBindFramebuffer(GL_FRAMEBUFFER, rt->id);
		size = rt->base.framebuffer_size;
	} else {
		_glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
	
	_glViewport(0, 0, (GLsizei)size.x, (GLsizei)size.y);
	_glScissor(0, 0, (GLsizei)size.x, (GLsizei)size.y);
	
	if(rt == NULL || rt->base.load != DRIFT_GFX_LOAD_ACTION_LOAD){
		DriftVec4 c = _command->clear_color;
		_glClearColor(c.r, c.g, c.b, c.a);
		_glClear(GL_COLOR_BUFFER_BIT);
	}
	
	state->extent = size;
	state->target = _command->rt;
}

static void DriftGLCommandSetScissor(const DriftGfxRenderer* renderer, const DriftGfxCommand* command, DriftGfxRenderState* state){
	const DriftGfxCommandScissor* _command = (DriftGfxCommandScissor*)command;
	
	DriftAABB2 bounds = _command->bounds;
	DriftVec2 max = state->extent;
	bounds.l = floorf(DriftClamp(bounds.l, 0, max.x));
	bounds.b = floorf(DriftClamp(bounds.b, 0, max.y));
	bounds.r = ceilf(DriftClamp(bounds.r, 0, max.x));
	bounds.t = ceilf(DriftClamp(bounds.t, 0, max.y));
	
	float bottom = state->target ? bounds.b : max.y - bounds.t;
	_glScissor((GLint)bounds.l, (GLint)bottom, (GLsizei)(bounds.r - bounds.l), (GLsizei)(bounds.t - bounds.b));
}

GLenum TextureTarget[] = {
	[DRIFT_GFX_TEXTURE_2D] = GL_TEXTURE_2D,
	[DRIFT_GFX_TEXTURE_2D_ARRAY] = GL_TEXTURE_2D_ARRAY,
};

static void DriftGLCommandBindPipeline(const DriftGfxRenderer* renderer, const DriftGfxCommand* command, DriftGfxRenderState* state){
	DriftGLRenderer* _renderer = (DriftGLRenderer*)renderer;
	DriftGfxCommandPipeline* _command = (DriftGfxCommandPipeline*)command;
	
	// TODO Does it make sense to cache the bindings?
	const DriftGfxPipelineBindings* bindings = _command->bindings;
	
	const DriftGfxPipeline* pipeline = _command->pipeline;
	const DriftGfxPipeline* current_pipeline = state->pipeline;
	if(pipeline != state->pipeline){
		const DriftGLShader* shader = (DriftGLShader*)pipeline->options.shader;
		if(&shader->base != state->pipeline->options.shader){
			_glUseProgram(shader->id);
		}
		
		const DriftGfxBlendMode* blend = pipeline->options.blend;
		if(blend != state->pipeline->options.blend){
			if(blend){
				_glEnable(GL_BLEND);
				_glBlendEquationSeparate(DriftGfxBlendOpToGL[blend->color_op], DriftGfxBlendOpToGL[blend->alpha_op]);
				_glBlendFuncSeparate(
					DriftGfxBlendFactorToGL[blend->color_src_factor], DriftGfxBlendFactorToGL[blend->color_dst_factor],
					DriftGfxBlendFactorToGL[blend->alpha_src_factor], DriftGfxBlendFactorToGL[blend->alpha_dst_factor]
				);
				
				if(blend->enable_blend_color){
					DriftVec4 c = bindings->blend_color;
					_glBlendColor(c.r, c.g, c.b, c.a);
				}
			} else {
				_glDisable(GL_BLEND);
			}
		}
		DRIFTGL_ASSERT_ERRORS();
		
		DriftGfxCullMode cull_mode = pipeline->options.cull_mode;
		if(cull_mode != state->pipeline->options.cull_mode){
			switch(cull_mode){
				case DRIFT_GFX_CULL_MODE_FRONT: {
					_glEnable(GL_CULL_FACE);
					_glCullFace(GL_BACK);
				} break;
				case DRIFT_GFX_CULL_MODE_BACK: {
					_glEnable(GL_CULL_FACE);
					_glCullFace(GL_FRONT);
				} break;
				default: {
					_glDisable(GL_CULL_FACE);
				} break;
			}
		}
	}
	
	const DriftGfxShaderDesc* desc = pipeline->options.shader->desc;
	size_t vertex_stride = desc->vertex_stride;
	size_t instance_stride = desc->instance_stride;
	for(int i = 0; i < DRIFT_GFX_VERTEX_ATTRIB_COUNT; i++){
		DriftGfxVertexAttrib attr = desc->vertex[i];
		if(attr.type != _DRIFT_GFX_TYPE_NONE){
			DRIFT_ASSERT(DriftGfxTypeMap[attr.type].size > 0, "Invalid vertex attrib type");
			_glEnableVertexAttribArray(i);
			size_t stride = (attr.instanced ? instance_stride : vertex_stride);
			void* ptr = (void*)(uintptr_t)(attr.instanced ? bindings->instance.offset : bindings->vertex.offset);
			if(DriftGfxTypeMap[attr.type].integer){
				_glVertexAttribIPointer(i, DriftGfxTypeMap[attr.type].size, DriftGfxTypeMap[attr.type].type, stride, ptr + attr.offset);
			} else {
				_glVertexAttribPointer(i, DriftGfxTypeMap[attr.type].size, DriftGfxTypeMap[attr.type].type, DriftGfxTypeMap[attr.type].normalized, stride, ptr + attr.offset);
			}
			_glVertexAttribDivisor(i, !!attr.instanced);
		} else {
			_glDisableVertexAttribArray(i);
		}
	}
	
	DriftGLShader* shader = (DriftGLShader*)pipeline->options.shader;
	const DriftGfxBufferBinding* uniforms = bindings->uniforms;
	for(uint i = 0; i < DRIFT_GFX_UNIFORM_BINDING_COUNT; i++){
		if(uniforms[i].size > 0) _glBindBufferRange(GL_UNIFORM_BUFFER, i, _renderer->uniform_buffer, uniforms[i].offset, uniforms[i].size);
	}
	
	DriftGLTexture** textures = (DriftGLTexture**)bindings->textures;
	DriftGLSampler** samplers = (DriftGLSampler**)bindings->samplers;
	for(uint i = 0; i < DRIFT_GFX_TEXTURE_BINDING_COUNT; i++){
		int texture_idx = shader->combined_mapping[i].texture;
		if(texture_idx >= 0){
			DriftGLTexture* texture = textures[texture_idx];
			DRIFT_ASSERT(texture, "Texture binding %d missing for '%s'", texture_idx, shader->base.name);
			_glActiveTexture(GL_TEXTURE0 + i);
			_glBindTexture(TextureTarget[texture->base.options.type], texture->id);
		}
		
		int sampler_idx = shader->combined_mapping[i].sampler;
		if(sampler_idx >= 0){
			DriftGLSampler* sampler = samplers[sampler_idx];
			DRIFT_ASSERT(sampler, "Sampler binding %d missing for '%s'", sampler_idx, shader->base.name);
			_glBindSampler(i, sampler ? (uintptr_t)sampler->id : 0);
		}
	}
	DRIFTGL_ASSERT_ERRORS();
	
	state->pipeline = _command->pipeline;
}

static void DriftGLCommandDrawIndexed(const DriftGfxRenderer* renderer, const DriftGfxCommand* command, DriftGfxRenderState* state){
	DriftGLRenderer* _renderer = (DriftGLRenderer*)renderer;
	DriftGfxCommandDraw* _command = (DriftGfxCommandDraw*)command;
	_glDrawElementsInstanced(GL_TRIANGLES, _command->index_count, GL_UNSIGNED_SHORT, (void*)(uintptr_t)_command->index_binding.offset, _command->instance_count);
	DRIFTGL_ASSERT_ERRORS();
}

static DriftGLRenderer* DriftGLRendererNew(void){
	DriftGLRenderer* renderer = DriftAlloc(DriftSystemMem, sizeof(DriftGLRenderer));
	(*renderer) = (DriftGLRenderer){};
	DriftGfxRendererInit(&renderer->base, (DriftGfxVTable){
		.bind_target = DriftGLCommandBindTarget,
		.set_scissor = DriftGLCommandSetScissor,
		.bind_pipeline = DriftGLCommandBindPipeline,
		.draw_indexed = DriftGLCommandDrawIndexed,
	});
	
	GLint uniform_align = 1;
	_glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &uniform_align);
	renderer->base.uniform_alignment = uniform_align;
	
	_glGenBuffers(1, &renderer->vertex_buffer);
	_glGenBuffers(1, &renderer->index_buffer);
	_glGenBuffers(1, &renderer->uniform_buffer);
	DriftGLBindBuffers(renderer, false);
	
	_glBufferData(GL_ARRAY_BUFFER, DRIFT_GFX_VERTEX_BUFFER_SIZE, NULL, GL_DYNAMIC_DRAW);
	_glBufferData(GL_ELEMENT_ARRAY_BUFFER, DRIFT_GFX_INDEX_BUFFER_SIZE, NULL, GL_DYNAMIC_DRAW);
	_glBufferData(GL_UNIFORM_BUFFER, DRIFT_GFX_UNIFORM_BUFFER_SIZE, NULL, GL_DYNAMIC_DRAW);
	DRIFTGL_ASSERT_ERRORS();
	
	DriftGLRendererMapAndUnbindBuffers(renderer);
	return renderer;
}

static void DriftGLShaderFree(const DriftGfxDriver* driver, void* obj){
	DriftSDLGLContext* ctx = driver->ctx;
	DriftGLShader* shader = obj;
	_glDeleteProgram(shader->id);
}

static DriftGfxShader* DriftGLShaderNew(const DriftGfxDriver* driver, const char* shader_name, const DriftGfxShaderDesc* desc, const DriftData vsource, const DriftData fsource){
	DriftSDLGLContext* ctx = driver->ctx;
	DriftGLShader* shader = DriftAlloc(DriftSystemMem, sizeof(*shader));
	DriftMapInsert(&ctx->destructors, (uintptr_t)shader, (uintptr_t)DriftGLShaderFree);
	
	(*shader) = (DriftGLShader){.base.desc = desc, .base.name = shader_name};
	for(uint i = 0; i < DRIFT_GFX_TEXTURE_BINDING_COUNT; i++){
		shader->combined_mapping[i].texture = -1;
		shader->combined_mapping[i].sampler = -1;
	}
	
	GLint uniform_align = 1;
	_glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &uniform_align);
	
	GLuint program = DriftGLCompileShader(vsource, fsource);
	if(program){
		shader->id = program;
		_glUseProgram(program);
		
		int blocks = 0;
		_glGetProgramiv(program, GL_ACTIVE_UNIFORM_BLOCKS, &blocks);
		for(int i = 0; i < blocks; i++){
			char uniform_name[256];
			GLsizei name_len = 0;
			_glGetActiveUniformBlockName(program, i, sizeof(uniform_name), &name_len, uniform_name);
			
			// Find uniform index.
			int uniform_idx = -1;
			for(uint i = 0; i < DRIFT_GFX_UNIFORM_BINDING_COUNT; i++){
				if(desc->uniform[i] && strcmp(uniform_name, desc->uniform[i]) == 0){
					uniform_idx = i;
					break;
				};
			}
			// DRIFT_ASSERT(uniform_idx >= 0, "Could not find uniform for '%s' in shader '%s'.", uniform_name, shader_name)
			if(uniform_idx >= 0) _glUniformBlockBinding(program, _glGetUniformBlockIndex(program, uniform_name), uniform_idx);
			DRIFTGL_ASSERT_ERRORS();
		}
		
		int uniform_count = 0;
		_glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &uniform_count);
		
		GLuint texture_binding = 0;
		for(int i = 0; i < uniform_count; i++){
			char texture_name[256];
			GLsizei name_len = 0;
			GLsizei size = 0;
			GLenum gl_type = 0;
			_glGetActiveUniform(program, i, sizeof(texture_name), &name_len, &size, &gl_type, texture_name);
			
			GLint location = _glGetUniformLocation(program, texture_name);
			if(location < 0) continue;
			
			// Chop off the SPIRV prefix.
			static const char SPIRV_PREFIX[] = "SPIRV_Cross_Combined";
			const size_t PREFIX_LEN = strlen(SPIRV_PREFIX);
			DRIFT_ASSERT(strncmp(texture_name, SPIRV_PREFIX, PREFIX_LEN) == 0, "Uniform missing SPIRV prefix. Not a texture+sampler?");
			const char* name_cursor = texture_name + PREFIX_LEN;
			
			DRIFT_ASSERT(texture_binding < DRIFT_GFX_TEXTURE_BINDING_COUNT, "Too many texture bindings.");
			
			// Find texture index.
			int texture_idx = -1;
			for(uint i = 0; i < DRIFT_GFX_TEXTURE_BINDING_COUNT; i++){
				const char* texture_name = desc->texture[i];
				if(texture_name){
					size_t len = strlen(texture_name);
					if(strncmp(name_cursor, texture_name, len) == 0){
						texture_idx = i;
						name_cursor += len;
						break;
					}
				}
			}
			DRIFT_ASSERT(texture_idx >= 0, "Could not find texture for '%s' in shader '%s'.", texture_name, shader_name)
			shader->combined_mapping[texture_binding].texture = texture_idx;
			
			// Find sampler index.
			int sampler_idx = -1;
			for(uint i = 0; i < DRIFT_GFX_SAMPLER_BINDING_COUNT; i++){
				const char* sampler_name = desc->sampler[i];
				if(sampler_name){
					if(strcmp(name_cursor, sampler_name) == 0){
						sampler_idx = i;
						break;
					}
				}
			}
			if(strcmp(name_cursor, "SPIRV_Cross_DummySampler") == 0) sampler_idx = 0;
			DRIFT_ASSERT(sampler_idx >= 0, "Could not find sampler for '%s' in shader '%s'.", texture_name, shader_name)
			shader->combined_mapping[texture_binding].sampler = sampler_idx;
			
			_glUniform1i(location, texture_binding);
			texture_binding++;
			
			DRIFTGL_ASSERT_ERRORS();
		}
		
		_glUseProgram(0);
		DRIFTGL_ASSERT_ERRORS();
		
		return &shader->base;
	} else {
		DRIFT_ABORT("SHADER FAIL");
		return 0;
	}
}

static const GLenum TextureInternalFormat[] = {
	[DRIFT_GFX_TEXTURE_FORMAT_RGBA8] = GL_RGBA8,
	[DRIFT_GFX_TEXTURE_FORMAT_RGBA16F] = GL_RGBA16F,
};

static const GLenum TextureFormat[] = {
	[DRIFT_GFX_TEXTURE_FORMAT_RGBA8] = GL_RGBA,
	[DRIFT_GFX_TEXTURE_FORMAT_RGBA16F] = GL_RGBA,
};

static const GLenum TextureType[] = {
	[DRIFT_GFX_TEXTURE_FORMAT_RGBA8] = GL_UNSIGNED_BYTE,
	[DRIFT_GFX_TEXTURE_FORMAT_RGBA16F] = GL_HALF_FLOAT,
};

static const GLenum TextureAddressMode[] = {
	[DRIFT_GFX_ADDRESS_MODE_CLAMP_TO_EDGE] = GL_CLAMP_TO_EDGE,
	[DRIFT_GFX_ADDRESS_MODE_CLAMP_TO_BORDER] = GL_CLAMP_TO_BORDER,
	[DRIFT_GFX_ADDRESS_MODE_REPEAT] = GL_REPEAT,
	[DRIFT_GFX_ADDRESS_MODE_MIRRORED_REPEAT] = GL_MIRRORED_REPEAT,
};

static const GLenum TextureFilters[3][3] = {
	[DRIFT_GFX_FILTER_NEAREST] = {
		[DRIFT_GFX_MIP_FILTER_NEAREST] = GL_NEAREST_MIPMAP_NEAREST,
		[DRIFT_GFX_MIP_FILTER_LINEAR] = GL_NEAREST_MIPMAP_LINEAR,
		[DRIFT_GFX_MIP_FILTER_NONE] = GL_NEAREST,
	},
	[DRIFT_GFX_FILTER_LINEAR] = {
		[DRIFT_GFX_MIP_FILTER_NEAREST] = GL_LINEAR_MIPMAP_NEAREST,
		[DRIFT_GFX_MIP_FILTER_LINEAR] = GL_LINEAR_MIPMAP_LINEAR,
		[DRIFT_GFX_MIP_FILTER_NONE] = GL_LINEAR,
	},
};

static void DriftGLSamplerFree(const DriftGfxDriver* driver, void* obj){
	DriftSDLGLContext* ctx = driver->ctx;
	DriftGLSampler* sampler = obj;
	_glDeleteSamplers(1, &sampler->id);
}

static DriftGfxSampler* DriftGLSamplerNew(const DriftGfxDriver* driver, DriftGfxSamplerOptions options){
	DriftAppAssertGfxThread();
	
	DriftSDLGLContext* ctx = driver->ctx;
	DriftGLSampler *sampler = DriftAlloc(DriftSystemMem, sizeof(*sampler));
	DriftMapInsert(&ctx->destructors, (uintptr_t)sampler, (uintptr_t)DriftGLSamplerFree);
	
	_glGenSamplers(1, &sampler->id);
	_glSamplerParameteri(sampler->id, GL_TEXTURE_WRAP_S, TextureAddressMode[options.address_x]);
	_glSamplerParameteri(sampler->id, GL_TEXTURE_WRAP_T, TextureAddressMode[options.address_y]);
	_glSamplerParameteri(sampler->id, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	_glSamplerParameteri(sampler->id, GL_TEXTURE_MIN_FILTER, TextureFilters[options.min_filter][options.mip_filter]);
	_glSamplerParameteri(sampler->id, GL_TEXTURE_MAG_FILTER, TextureFilters[options.mag_filter][DRIFT_GFX_MIP_FILTER_NONE]);
	DRIFTGL_ASSERT_ERRORS();
	
	return &sampler->base;
}

static void DriftGLTextureFree(const DriftGfxDriver* driver, void* obj){
	DriftSDLGLContext* ctx = driver->ctx;
	DriftGLTexture* texture = obj;
	_glDeleteTextures(1, &texture->id);
}

static DriftGfxTexture* DriftGLTextureNew(const DriftGfxDriver* driver, uint width, uint height, DriftGfxTextureOptions options){
	DriftAppAssertGfxThread();
	
	DriftSDLGLContext* ctx = driver->ctx;
	DriftGLTexture* texture = DriftAlloc(DriftSystemMem, sizeof(*texture));
	DriftMapInsert(&ctx->destructors, (uintptr_t)texture, (uintptr_t)DriftGLTextureFree);
	
	texture->base.options = options;
	texture->base.width = width;
	texture->base.height = height;
	_glGenTextures(1, &texture->id);
	
	GLenum target = TextureTarget[options.type];
	GLenum internalFormat = TextureInternalFormat[options.format];
	GLenum format = TextureFormat[options.format];
	GLenum type = TextureType[options.format];

	_glBindTexture(target, texture->id);
	switch(options.type){
		case DRIFT_GFX_TEXTURE_2D: {
			_glTexImage2D(target, 0, internalFormat, (GLint)width, (GLint)height, 0, format, type, NULL);
		} break;
		case DRIFT_GFX_TEXTURE_2D_ARRAY: {
			_glTexImage3D(target, 0, internalFormat, (GLint)width, (GLint)height, options.layers, 0, format, type, NULL);
		} break;
		default: DRIFT_ABORT("Invalid enumeration.");
	}
	DRIFTGL_ASSERT_ERRORS();
	
	_glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, 0);
	_glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	_glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	_glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	_glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	_glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		
	_glBindTexture(target, 0);
	DRIFTGL_ASSERT_ERRORS();
	
	return &texture->base;
}

static void DriftGLLoadTextureLayer(const DriftGfxDriver* driver, DriftGfxTexture* texture, uint layer, const void* pixels){
	DriftAppAssertGfxThread();
	
	DriftGLTexture* _texture = (DriftGLTexture*)texture;
	GLenum target = TextureTarget[texture->options.type];
	GLenum format = TextureFormat[texture->options.format];
	GLenum type = TextureType[texture->options.format];
	
	_glBindTexture(target, _texture->id);
	switch(texture->options.type){
		case DRIFT_GFX_TEXTURE_2D: {
			_glTexSubImage2D(target, 0, 0, 0, texture->width, texture->height, format, type, pixels);
		} break;
		case DRIFT_GFX_TEXTURE_2D_ARRAY: {
			_glTexSubImage3D(target, 0, 0, 0, layer, texture->width, texture->height, 1, format, type, pixels);
		} break;
		default: DRIFT_ABORT("Invalid enumeration.");
	}
	_glBindTexture(target, 0);
	DRIFTGL_ASSERT_ERRORS();
}

static void DriftGLRenderTargetFree(const DriftGfxDriver* driver, void* obj){
	DriftSDLGLContext* ctx = driver->ctx;
	DriftGLRenderTarget* rt = obj;
	_glDeleteFramebuffers(1, &rt->id);
}

static DriftGfxRenderTarget* DriftGLRenderTargetNew(const DriftGfxDriver* driver, DriftGfxRenderTargetOptions options){
	DriftAppAssertGfxThread();
	
	DriftSDLGLContext* ctx = driver->ctx;
	DriftGLRenderTarget* rt = DriftAlloc(DriftSystemMem, sizeof(*rt));
	DriftMapInsert(&ctx->destructors, (uintptr_t)rt, (uintptr_t)DriftGLRenderTargetFree);
	
	_glGenFramebuffers(1, &rt->id);
	_glBindFramebuffer(GL_FRAMEBUFFER, rt->id);
	
	GLsizei buffer_count = 0;
	GLenum buffers[DRIFT_GFX_RENDER_TARGET_COUNT];
	
	for(int i = 0; i < DRIFT_GFX_RENDER_TARGET_COUNT; i++){
		DriftGLTexture* texture = (DriftGLTexture*)options.bindings[i].texture;
		if(texture){
			switch(texture->base.options.type){
				case DRIFT_GFX_TEXTURE_2D: {
					_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, texture->id, 0);
				} break;
				case DRIFT_GFX_TEXTURE_2D_ARRAY: {
					_glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, texture->id, 0, options.bindings[i].layer);
				} break;
				default: DRIFT_ABORT("Invalid enumeration.");
			}
			DRIFTGL_ASSERT_ERRORS();
			
			buffers[buffer_count++] = GL_COLOR_ATTACHMENT0 + i;
			rt->base.framebuffer_size = (DriftVec2){texture->base.width, texture->base.height};
		}
	}
	GLenum status = _glCheckFramebufferStatus(GL_FRAMEBUFFER);
	DRIFT_ASSERT(status == GL_FRAMEBUFFER_COMPLETE, "Render texture creation failed. (0x%X)", status);
	
	_glDrawBuffers(buffer_count, buffers);
	_glBindFramebuffer(GL_FRAMEBUFFER, 0);
	DRIFTGL_ASSERT_ERRORS();
	
	rt->base.load = options.load;
	rt->base.store = options.store;
	return &rt->base;
}

static DriftGfxShader* DriftGLShaderLoad(const DriftGfxDriver* driver, const char* name, const DriftGfxShaderDesc* desc){
	DriftAppAssertGfxThread();
	
	u8 buffer[64*1024];
	DriftMem* mem = DriftLinearMemInit(buffer, sizeof(buffer), "Shader Mem");
	DriftData vshader = DriftAssetLoad(mem, "shaders/%s%s", name, ".vert");
	DriftData fshader = DriftAssetLoad(mem, "shaders/%s%s", name, ".frag");
	return DriftGLShaderNew(driver, name, desc, vshader, fshader);
}

static void DriftGLPipelineFree(const DriftGfxDriver* driver, void* obj){}

static DriftGfxPipeline* DriftGLPipelineNew(const DriftGfxDriver* driver, DriftGfxPipelineOptions options){
	DriftSDLGLContext* ctx = driver->ctx;
	DriftGfxPipeline* pipeline = DriftAlloc(DriftSystemMem, sizeof(DriftGfxPipeline));
	DriftMapInsert(&ctx->destructors, (uintptr_t)pipeline, (uintptr_t)DriftGLPipelineFree);
	
	(*pipeline) = (DriftGfxPipeline){.options = options};
	return pipeline;
}

static void DriftGLFreeObjects(const DriftGfxDriver* driver, void* obj[], uint count){
	DriftAppAssertGfxThread();
	
	DriftSDLGLContext* ctx = driver->ctx;
	DriftGfxFreeObjects(driver, &ctx->destructors, obj, count);
}

static void DriftGLFreeAll(const DriftGfxDriver* driver){
	DriftAppAssertGfxThread();
	
	DriftSDLGLContext* ctx = driver->ctx;
	DriftGfxFreeAll(driver, &ctx->destructors);
}

static void DriftSDLGLInitContext(tina_job* job){
	DriftApp* app = tina_job_get_description(job)->user_data;
	DriftSDLGLContext* ctx = app->shell_context;
	
	DRIFT_ASSERT_HARD(SDL_GL_MakeCurrent(app->shell_window, ctx->gl_context) == 0, "Failed to bind OpenGL context: %s", SDL_GetError());
	SDL_GL_SetSwapInterval(1);
	DriftLoadGL();
	
	GLint major, minor;
	_glGetIntegerv(GL_MAJOR_VERSION, &major);
	_glGetIntegerv(GL_MINOR_VERSION, &minor);
	DRIFT_LOG("GL %d.%d initialized:", major, minor);
	DRIFT_LOG("	vendor: %s", _glGetString(GL_VENDOR));
	DRIFT_LOG("	renderer: %s", _glGetString(GL_RENDERER));
	DRIFT_LOG("	version: %s, GLSL: %s", _glGetString(GL_VERSION), _glGetString(GL_SHADING_LANGUAGE_VERSION));
	
	GLuint vao;
	_glGenVertexArrays(1, &vao);
	_glBindVertexArray(vao);
	
	DriftMapInit(&ctx->destructors, DriftSystemMem, "#GLDestructors", 0);
	
	for(uint i = 0; i < DRIFT_GL_RENDERER_COUNT; i++) ctx->renderers[i] = DriftGLRendererNew();
}

void* DriftShellSDLGL(DriftApp* app, DriftShellEvent event, void* shell_value){
	switch(event){
		case DRIFT_SHELL_START:{
			SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
			int err = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER);
			DRIFT_ASSERT(err == 0, "SDL_Init() error: %s", SDL_GetError());
			
			SDL_version sdl_version;
			SDL_GetVersion(&sdl_version);
			DRIFT_LOG("Using SDL v%d.%d.%d", sdl_version.major, sdl_version.minor, sdl_version.patch);
			
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
			SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
			SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
			SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
			SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
			SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
			
			if(app->window_w == 0){
				app->window_x = SDL_WINDOWPOS_CENTERED;
				app->window_y = SDL_WINDOWPOS_CENTERED;
				app->window_w = DRIFT_APP_DEFAULT_SCREEN_W;
				app->window_h = DRIFT_APP_DEFAULT_SCREEN_H;
			}
			
			u32 window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN;
			if(app->fullscreen) window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
			
			app->shell_window = SDL_CreateWindow("Veridian Expanse", app->window_x, app->window_y, app->window_w, app->window_h, window_flags);
			DRIFT_ASSERT_HARD(app->shell_window, "Failed to create SDL window.");
			
			SDL_PumpEvents();
			SDL_SetWindowPosition(app->shell_window, app->window_x, app->window_y);
			
			DriftSDLGLContext* ctx = DriftAlloc(DriftSystemMem, sizeof(*ctx));
			DRIFT_ASSERT_HARD(ctx->gl_context = SDL_GL_CreateContext(app->shell_window), "Failed to create OpenGL context: %s", SDL_GetError());
			DRIFT_ASSERT_HARD(ctx->sync_context = SDL_GL_CreateContext(app->shell_window), "Failed to create OpenGL sync context: %s", SDL_GetError());
			// Leave sync context bound on main thread.
			app->shell_context = ctx;
			
			DriftGfxDriver* driver = DriftAlloc(DriftSystemMem, sizeof(*driver));
			(*driver) = (DriftGfxDriver){
				.ctx = app->shell_context,
				.load_shader = DriftGLShaderLoad,
				.new_pipeline = DriftGLPipelineNew,
				.new_sampler = DriftGLSamplerNew,
				.new_texture = DriftGLTextureNew,
				.new_target = DriftGLRenderTargetNew,
				.load_texture_layer = DriftGLLoadTextureLayer,
				.free_objects = DriftGLFreeObjects,
				.free_all = DriftGLFreeAll,
			};
			app->gfx_driver = driver;
			tina_scheduler_enqueue(app->scheduler, DriftSDLGLInitContext, app, 0, DRIFT_JOB_QUEUE_GFX, NULL);
		} break;
		
		case DRIFT_SHELL_SHOW_WINDOW: SDL_ShowWindow(app->shell_window); break;
		
		case DRIFT_SHELL_STOP:{
			DRIFT_LOG("SDL Shutdown.");
			DriftSDLGLContext* ctx = app->shell_context;
			SDL_GL_DeleteContext(ctx->gl_context);
			SDL_Quit();
		} break;
		
		case DRIFT_SHELL_BEGIN_FRAME:{
			DriftSDLGLContext* ctx = app->shell_context;
			DriftGLRenderer* renderer = ctx->renderers[ctx->renderer_index++ & (DRIFT_GL_RENDERER_COUNT - 1)];
			TracyCZoneN(ZONE_WAIT, "Wait", true);
			DriftGLRendererWait(renderer);
			TracyCZoneEnd(ZONE_WAIT);

			SDL_GetWindowPosition(app->shell_window, &app->window_x, &app->window_y);
			SDL_GetWindowSize(app->shell_window, &app->window_w, &app->window_h);
			
			int w, h;
			SDL_GL_GetDrawableSize(app->shell_window, &w, &h);
			DriftGfxRendererPrepare((DriftGfxRenderer*)renderer, (DriftVec2){w, h}, shell_value);
			
			return renderer;
		} break;
		
		case DRIFT_SHELL_PRESENT_FRAME:{
			DriftGLRenderer* renderer = shell_value;
			
			TracyCZoneN(ZONE_EXECUTE, "GLExecute", true);
			DriftGLRendererExecute(renderer);
			TracyCZoneEnd(ZONE_EXECUTE);
			
			TracyCZoneN(ZONE_SWAP, "Swap", true);
			SDL_GL_SwapWindow(app->shell_window);
			TracyCZoneEnd(ZONE_SWAP);
		} break;
		
		case DRIFT_SHELL_TOGGLE_FULLSCREEN:{
			SDL_SetWindowFullscreen(app->shell_window, app->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
		} break;
	}
	
	return NULL;
}

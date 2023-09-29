/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <string.h>

#include <volk/volk.h>
#include <SDL.h>
#include <SDL_vulkan.h>
#include <tracy/TracyC.h>

#include "drift_base.h"
#include "drift_gfx_internal.h"

#define DRIFT_VULKAN_VALIDATE DRIFT_DEBUG

static const char* VALIDATION_LAYERS[] = {"VK_LAYER_KHRONOS_validation"};
static const u32 VALIDATION_LAYERS_COUNT = sizeof(VALIDATION_LAYERS)/sizeof(*VALIDATION_LAYERS);

static const char* REQUIRED_EXTENSIONS[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_MAINTENANCE1_EXTENSION_NAME};
static const u32 REQUIRED_EXTENSIONS_COUNT = sizeof(REQUIRED_EXTENSIONS)/sizeof(*REQUIRED_EXTENSIONS);

#define DRIFT_VK_STAGING_BUFFER_SIZE (2*1024*1024)
#define DRIFT_VK_STAGING_JOB_COUNT 32
#define MAX_SWAP_CHAIN_IMAGE_COUNT 8
#define DRIFT_VK_RENDERER_COUNT 4

typedef struct {
	int graphics_idx;
	int present_idx;
} DriftVkQueueFamilies;

typedef struct DriftVkRenderer DriftVkRenderer;

typedef struct {
	VkDeviceMemory memory;
	VkBuffer buffer;
	void* ptr;
} DriftVkBuffer;

typedef struct {
	size_t offset, advance;
	VkFence fence;
	VkCommandBuffer command_buffer;
} DriftVkStagingJob;

typedef struct {
	VkExtent2D extent;
	VkSwapchainKHR swap_chain;
	VkSurfaceFormatKHR format;
	VkPresentModeKHR present_mode;
	VkImage images[MAX_SWAP_CHAIN_IMAGE_COUNT];
	VkImageView views[MAX_SWAP_CHAIN_IMAGE_COUNT];
	VkFramebuffer framebuffers[MAX_SWAP_CHAIN_IMAGE_COUNT];
	VkSemaphore semaphores[MAX_SWAP_CHAIN_IMAGE_COUNT];
	u32 image_count, image_index;
	VkFence fence;
} DriftVkSwapChain;

typedef struct {
	VkInstance instance;
	VkPhysicalDevice physical_device;
	VkPhysicalDeviceProperties physical_properties;
	VkSurfaceKHR surface;
	DriftVkQueueFamilies queue_families;
	VkDevice device;
	VkQueue graphics_queue, present_queue;
	VkRenderPass render_pass;
	
	DriftMap destructors;
	VkDebugUtilsMessengerEXT messenger;
	
	DriftVkSwapChain swap_chain;
	
	VkCommandPool command_pool;
	struct {
		DriftVkBuffer buffer;
		size_t buffer_write, buffer_read;
		DriftVkStagingJob jobs[DRIFT_VK_STAGING_JOB_COUNT];
		uint job_head, job_tail;
	} staging;
	
	DriftVkRenderer* renderers[DRIFT_VK_RENDERER_COUNT];
	uint renderer_index;
} DriftVkContext;

typedef struct {
	DriftGfxSampler base;
	VkSampler sampler;
} DriftVkSampler;

typedef struct {
	DriftGfxTexture base;
	
	VkDeviceMemory memory;
	VkImage image;
	VkImageView view;
} DriftVkTexture;

typedef struct {
	DriftGfxShader base;
	VkShaderModule vshader, fshader;
} DriftVkShader;

typedef struct {
	DriftGfxPipeline base;
	VkDescriptorSetLayout descriptor_set_layout;
	VkPipelineLayout pipeline_layout;
	VkPipeline pipeline;
} DriftVkPipeline;

typedef struct {
	DriftGfxRenderTarget base;
	uint attachment_count;
	VkImageView views[DRIFT_GFX_RENDER_TARGET_COUNT];
	VkRenderPass render_pass;
	VkFramebuffer framebuffer;
} DriftVkRenderTarget;

struct DriftVkRenderer {
	DriftGfxRenderer base;
	
	DriftVkContext* ctx;
	VkRenderPass current_pass;
	
	DriftVkBuffer buffer;
	VkDescriptorPool descriptor_pool;
	VkCommandBuffer command_buffer;
	VkFence command_fence;
};

static void AssertSuccess(VkResult result, const char* msg){
	DRIFT_ASSERT(result == VK_SUCCESS, "Vulkan Error (%d): %s", result, msg);
}

static void NameObject(DriftVkContext* ctx, VkObjectType type, u64 handle, const char* format, ...){
#if DRIFT_DEBUG
	char name[256];
	
	va_list args;
	va_start(args, format);
	vsnprintf(name, sizeof(name), format, args);
	va_end(args);
	
	PFN_vkSetDebugUtilsObjectNameEXT set_name = (void*)vkGetDeviceProcAddr(ctx->device, "vkSetDebugUtilsObjectNameEXT");
	DRIFT_ASSERT(set_name, "vkSetDebugUtilsObjectNameEXT() not found.");
	set_name(ctx->device, &(VkDebugUtilsObjectNameInfoEXT){
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
		.objectType = type, .objectHandle = handle, .pObjectName = name,
	});
#endif
}

DriftVkQueueFamilies find_queue_families(DriftVkContext* ctx, VkPhysicalDevice device){
	DriftVkQueueFamilies indices = {.graphics_idx = -1, .present_idx = -1};
	
	u32 queue_family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, NULL);
	VkQueueFamilyProperties queue_families[queue_family_count];
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families);
	
	for(unsigned i = 0; i < queue_family_count; i++){
		if(queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT){
			indices.graphics_idx = i;
		}
		
		VkBool32 present_support = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(device, i, ctx->surface, &present_support);
		if(present_support) indices.present_idx = i;
	}
	
	return indices;
}

static bool is_device_suitable(DriftVkContext* ctx, VkPhysicalDevice device){
	VkPhysicalDeviceProperties device_properties;
	vkGetPhysicalDeviceProperties(device, &device_properties);
	
	if(
		device_properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU &&
		device_properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
	) return false;
	
	DriftVkQueueFamilies families = find_queue_families(ctx, device);
	if(families.graphics_idx < 0 || families.present_idx < 0) return false;
	
	u32 formats_count = 0, present_modes_count = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(device, ctx->surface, &formats_count, NULL);
	vkGetPhysicalDeviceSurfacePresentModesKHR(device, ctx->surface, &present_modes_count, NULL);
	return formats_count > 0 && present_modes_count > 0;
}

// This function is basically straight out of the VkPhysicalDeviceMemoryProperties docs.
static uint32_t DriftVkFindMemoryType(DriftVkContext* ctx, uint32_t required_mem_type_mask, VkMemoryPropertyFlags required_properties){
	VkPhysicalDeviceMemoryProperties properties;
	vkGetPhysicalDeviceMemoryProperties(ctx->physical_device, &properties);
	
	for(unsigned i = 0; i < properties.memoryTypeCount; i++){
		// Check if this memory type can store the object (ex: vertex buffer) that we need.
		bool can_use_type = required_mem_type_mask & (1 << i);
		// Check that the memory type has all the properties we as the developer need.
		bool has_required_properties = (properties.memoryTypes[i].propertyFlags & required_properties) == required_properties;
		if(can_use_type && has_required_properties) return i;
	}
	
	DRIFT_ABORT("Failed to allocate Vulkan memory");
}

static VkExtent2D DriftVkCreateSwapChain(DriftVkContext* ctx, DriftVec2 fb_extent){
	u32 formats_count = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical_device, ctx->surface, &formats_count, NULL);
	VkSurfaceFormatKHR formats[formats_count];
	vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical_device, ctx->surface, &formats_count, formats);
	
	// TODO Do I need to make this fancier?
	ctx->swap_chain.format = formats[0];
	for(uint i = 0; i < formats_count; i++){
		if(formats[i].format == VK_FORMAT_B8G8R8A8_UNORM){
			ctx->swap_chain.format = formats[i];
			break;
		}
	}
	
	u32 modes_count;
	vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physical_device, ctx->surface, &modes_count, NULL);
	VkPresentModeKHR modes[modes_count];
	vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physical_device, ctx->surface, &modes_count, modes);
	ctx->swap_chain.present_mode = VK_PRESENT_MODE_FIFO_KHR;

	VkSurfaceCapabilitiesKHR capabilities;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->physical_device, ctx->surface, &capabilities);
	
	VkExtent2D extent = capabilities.currentExtent, min = capabilities.minImageExtent, max = capabilities.maxImageExtent;
	if(extent.width == ~0u) extent.width = DriftClamp(fb_extent.x, min.width, max.width);
	if(extent.height == ~0u) extent.height = DriftClamp(fb_extent.y, min.height, max.height);
	ctx->swap_chain.extent = extent;
	
	// Require at least 2 swap images.
	uint image_count = DRIFT_MAX(2, capabilities.minImageCount);
	DRIFT_ASSERT_HARD(capabilities.maxImageCount == 0 || capabilities.maxImageCount >= 2, "Must support at least 2 swap images.");
	
	VkSwapchainCreateInfoKHR info = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = ctx->surface, .minImageCount = image_count,
		.imageFormat = ctx->swap_chain.format.format,
		.imageColorSpace = ctx->swap_chain.format.colorSpace,
		.imageExtent = extent, .imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.preTransform = capabilities.currentTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = ctx->swap_chain.present_mode,
		.clipped = VK_TRUE, .oldSwapchain = VK_NULL_HANDLE,
	};
	
	u32 queue_indices[] = {ctx->queue_families.graphics_idx, ctx->queue_families.present_idx};
	if(ctx->queue_families.graphics_idx != ctx->queue_families.present_idx){
		info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		info.queueFamilyIndexCount = 2;
		info.pQueueFamilyIndices = queue_indices;
	}
	
	VkResult result = vkCreateSwapchainKHR(ctx->device, &info, NULL, &ctx->swap_chain.swap_chain);
	AssertSuccess(result, "Failed to create Vulkan swapchain.");
	
	VkAttachmentDescription color_attachment = {
		.format = ctx->swap_chain.format.format, .samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
	};
	
	VkAttachmentReference color_ref = {.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	VkSubpassDescription subpass = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .pColorAttachments = &color_ref, .colorAttachmentCount = 1,
	};
	
	VkSubpassDependency dependency = {
		.srcSubpass = VK_SUBPASS_EXTERNAL, .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, .srcAccessMask = 0,
		.dstSubpass = 0, .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
	};
	
	result = vkCreateRenderPass(ctx->device, &(VkRenderPassCreateInfo){
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.pAttachments = &color_attachment, .attachmentCount = 1,
		.pSubpasses = &subpass, .subpassCount = 1,
		.pDependencies = &dependency, .dependencyCount = 1,
	}, NULL, &ctx->render_pass);
	AssertSuccess(result, "Failed to create Vulkan renderpass.");
	NameObject(ctx, VK_OBJECT_TYPE_RENDER_PASS, (u64)ctx->render_pass, "Default render pass");
	
	vkGetSwapchainImagesKHR(ctx->device, ctx->swap_chain.swap_chain, &ctx->swap_chain.image_count, NULL);
	DRIFT_ASSERT(ctx->swap_chain.image_count <= MAX_SWAP_CHAIN_IMAGE_COUNT, "Vulkan swap chain is huge?!");
	vkGetSwapchainImagesKHR(ctx->device, ctx->swap_chain.swap_chain, &ctx->swap_chain.image_count, ctx->swap_chain.images);
	
	result = vkCreateFence(ctx->device, &(VkFenceCreateInfo){
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
	}, NULL, &ctx->swap_chain.fence);
	AssertSuccess(result, "Failed to create Vulkan fence.");
	NameObject(ctx, VK_OBJECT_TYPE_FENCE, (u64)ctx->swap_chain.fence, "Swap chain fence");
	
	for(unsigned i = 0; i < ctx->swap_chain.image_count; i++){
		result = vkCreateImageView(ctx->device, &(VkImageViewCreateInfo){
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = ctx->swap_chain.images[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = ctx->swap_chain.format.format,
			.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.subresourceRange.baseMipLevel = 0, .subresourceRange.levelCount = 1,
			.subresourceRange.baseArrayLayer = 0, .subresourceRange.layerCount = 1,
		}, NULL, ctx->swap_chain.views + i);
		AssertSuccess(result, "Failed to create Vulkan swapchain image view.");
		NameObject(ctx, VK_OBJECT_TYPE_IMAGE_VIEW, (u64)ctx->swap_chain.views[i], "Swap %d view", i);
		
		result = vkCreateFramebuffer(ctx->device, &(VkFramebufferCreateInfo){
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = ctx->render_pass,
			.pAttachments = ctx->swap_chain.views + i, .attachmentCount = 1,
			.width = info.imageExtent.width, .height = info.imageExtent.height, .layers = 1,
		}, NULL, ctx->swap_chain.framebuffers + i);
		AssertSuccess(result, "Failed te create Vulkan swapchain framebuffer.");
		NameObject(ctx, VK_OBJECT_TYPE_FRAMEBUFFER, (u64)ctx->swap_chain.framebuffers[i], "Swap %d framebuffer", i);
		
		result = vkCreateSemaphore(ctx->device, &(VkSemaphoreCreateInfo){
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		}, NULL, &ctx->swap_chain.semaphores[i]);
		AssertSuccess(result, "Failed to create Vulkan swapchain semaphore.");
		NameObject(ctx, VK_OBJECT_TYPE_SEMAPHORE, (u64)ctx->swap_chain.semaphores[i], "Swap %d semaphore", i);
	}
	
	return extent;
}

static void DriftVkDestroySwapChain(DriftVkContext* ctx){
	for(uint i = 0; i < ctx->swap_chain.image_count; i++){
		vkDestroySemaphore(ctx->device, ctx->swap_chain.semaphores[i], NULL);
		vkDestroyFramebuffer(ctx->device, ctx->swap_chain.framebuffers[i], NULL);
		vkDestroyImageView(ctx->device, ctx->swap_chain.views[i], NULL);
	}
	
	vkDestroyFence(ctx->device, ctx->swap_chain.fence, NULL);
	vkDestroyRenderPass(ctx->device, ctx->render_pass, NULL);
	vkDestroySwapchainKHR(ctx->device, ctx->swap_chain.swap_chain, NULL);
}

static void DriftVkRendererBindTarget(const DriftGfxRenderer* renderer, const DriftGfxCommand* command, DriftGfxRenderState* state){
	DriftVkRenderer* _renderer = (DriftVkRenderer*)renderer;
	DriftVkContext* ctx = _renderer->ctx;
	const DriftGfxCommandTarget* _command = (DriftGfxCommandTarget*)command;
	const DriftVkRenderTarget* _target = (DriftVkRenderTarget*)_command->rt;
	
	VkRenderPass pass = ctx->render_pass;
	VkFramebuffer fb = ctx->swap_chain.framebuffers[ctx->swap_chain.image_index];
	DriftVec2 size = renderer->default_extent;
	VkViewport viewport = {.width = size.x, .height = -size.y, .y = size.y};
	DriftVec4 c = _command->clear_color;
	uint count = 1;
	
	if(_target){
		pass = _target->render_pass;
		fb = _target->framebuffer;
		size = _target->base.framebuffer_size;
		viewport = (VkViewport){.width = size.x, .height = size.y};
		count = _target->attachment_count;
	}
	
	if(_renderer->current_pass) vkCmdEndRenderPass(_renderer->command_buffer);
	
	VkClearValue clear[DRIFT_GFX_RENDER_TARGET_COUNT];
	for(uint i = 0; i < DRIFT_GFX_RENDER_TARGET_COUNT; i++) clear[i] = (VkClearValue){.color.float32 = {c.r, c.g, c.b, c.a}};
	
	VkExtent2D extent = {(uint)size.x, (uint)size.y};
	
	vkCmdBeginRenderPass(_renderer->command_buffer, &(VkRenderPassBeginInfo){
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = pass, .framebuffer = fb, .renderArea.extent = extent,
		.pClearValues = clear, .clearValueCount = count,
	}, VK_SUBPASS_CONTENTS_INLINE);
	_renderer->current_pass = pass;
	
	vkCmdSetViewport(_renderer->command_buffer, 0, 1, &viewport);
	vkCmdSetScissor(_renderer->command_buffer, 0, 1, &(VkRect2D){.extent = extent});
	
	state->extent = size;
	state->target = _command->rt;
}

static void DriftVkCommandSetScissor(const DriftGfxRenderer* renderer, const DriftGfxCommand* command, DriftGfxRenderState* state){
	DriftVkRenderer* _renderer = (DriftVkRenderer*)renderer;
	const DriftGfxCommandScissor* _command = (DriftGfxCommandScissor*)command;
	
	DriftAABB2 bounds = _command->bounds;
	DriftVec2 max = state->extent;
	bounds.l = floorf(DriftClamp(bounds.l, 0, max.x));
	bounds.b = floorf(DriftClamp(bounds.b, 0, max.y));
	bounds.r = ceilf(DriftClamp(bounds.r, 0, max.x));
	bounds.t = ceilf(DriftClamp(bounds.t, 0, max.y));
	vkCmdSetScissor(_renderer->command_buffer, 0, 1, &(VkRect2D){
		.offset = {(int)bounds.l, (int)bounds.b},
		.extent = {(uint)(bounds.r - bounds.l), (uint)(bounds.t - bounds.b)},
	});
}

static inline VkWriteDescriptorSet* PushDescriptor(VkWriteDescriptorSet* writes, VkDescriptorSet set, VkDescriptorType type, uint binding, void* info){
	(*writes) = (VkWriteDescriptorSet){
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .pBufferInfo = info, .pImageInfo = info,
		.dstSet = set, .descriptorType = type, .dstBinding = binding, .descriptorCount = 1,
	};
	return writes + 1;
}

static void DriftVkRendererBindPipeline(const DriftGfxRenderer* renderer, const DriftGfxCommand* command, DriftGfxRenderState* state){
	DriftVkRenderer* _renderer = (DriftVkRenderer*)renderer;
	VkDevice device = _renderer->ctx->device;
	DriftGfxCommandPipeline* _command = (DriftGfxCommandPipeline*)command;
	DriftVkPipeline* _pipeline = (DriftVkPipeline*)_command->pipeline;
	
	// TODO Avoid redundant binds or set allocations? Pretty sure that never actually happens anyway...
	vkCmdBindPipeline(_renderer->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline->pipeline);
	
	VkDescriptorSet set;
	VkResult result = vkAllocateDescriptorSets(device, &(VkDescriptorSetAllocateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = _renderer->descriptor_pool,
		.pSetLayouts = &_pipeline->descriptor_set_layout, .descriptorSetCount = 1,
	}, &set);
	
	const DriftGfxPipelineBindings* bindings = _command->bindings;
	VkWriteDescriptorSet writes[DRIFT_GFX_UNIFORM_BINDING_COUNT + DRIFT_GFX_SAMPLER_BINDING_COUNT + DRIFT_GFX_TEXTURE_BINDING_COUNT];
	VkWriteDescriptorSet* cursor = writes;
	
	VkDescriptorBufferInfo buffer_infos[DRIFT_GFX_UNIFORM_BINDING_COUNT];
	for(uint i = 0; i < DRIFT_GFX_UNIFORM_BINDING_COUNT; i++){
		DriftGfxBufferBinding uniform = bindings->uniforms[i];
		if(uniform.size){
			const size_t UNIFORMS_OFFSET = DRIFT_GFX_VERTEX_BUFFER_SIZE + DRIFT_GFX_INDEX_BUFFER_SIZE;
			buffer_infos[i] = (VkDescriptorBufferInfo){.buffer = _renderer->buffer.buffer, .offset = uniform.offset + UNIFORMS_OFFSET, .range = uniform.size},
			cursor = PushDescriptor(cursor, set, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, i, buffer_infos + i);
		}
	}
	
	VkDescriptorImageInfo sampler_infos[DRIFT_GFX_SAMPLER_BINDING_COUNT];
	for(uint i = 0; i < DRIFT_GFX_SAMPLER_BINDING_COUNT; i++){
		DriftVkSampler* sampler = (DriftVkSampler*)bindings->samplers[i];
		if(sampler){
			sampler_infos[i] = (VkDescriptorImageInfo){.sampler = sampler->sampler};
			cursor = PushDescriptor(cursor, set, VK_DESCRIPTOR_TYPE_SAMPLER, i + 4, sampler_infos + i);
		}
	}
	
	VkDescriptorImageInfo texture_infos[DRIFT_GFX_TEXTURE_BINDING_COUNT];
	for(uint i = 0; i < DRIFT_GFX_TEXTURE_BINDING_COUNT; i++){
		DriftVkTexture* texture = (DriftVkTexture*)bindings->textures[i];
		if(texture){
			texture_infos[i] = (VkDescriptorImageInfo){.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .imageView = texture->view},
			cursor = PushDescriptor(cursor, set, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, i + 8, texture_infos + i);
		}
	}
	
	vkUpdateDescriptorSets(device, cursor - writes, writes, 0, NULL);
	vkCmdBindDescriptorSets(_renderer->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline->pipeline_layout, 0, 1, &set, 0, NULL);
	
	VkBuffer vbuffer = _renderer->buffer.buffer;
	vkCmdBindVertexBuffers(_renderer->command_buffer, 0, 2, (VkBuffer[]){vbuffer, vbuffer}, (u64[]){bindings->vertex.offset, bindings->instance.offset});
	
	const DriftGfxBlendMode* blend = _pipeline->base.options.blend;
	if(blend && blend->enable_blend_color) vkCmdSetBlendConstants(_renderer->command_buffer, (float*)&bindings->blend_color);
	
	state->pipeline = _command->pipeline;
}

static void DriftVkRendererDrawIndexed(const DriftGfxRenderer* renderer, const DriftGfxCommand* command, DriftGfxRenderState* state){
	DriftVkRenderer* _renderer = (DriftVkRenderer*)renderer;
	DriftGfxCommandDraw* _command = (DriftGfxCommandDraw*)command;
	
	vkCmdBindIndexBuffer(_renderer->command_buffer, _renderer->buffer.buffer, _command->index_binding.offset + DRIFT_GFX_VERTEX_BUFFER_SIZE, VK_INDEX_TYPE_UINT16);
	vkCmdDrawIndexed(_renderer->command_buffer, _command->index_count, _command->instance_count, 0, 0, 0);
}

static VkBool32 VulkanErrorCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT severity,
	VkDebugUtilsMessageTypeFlagsEXT type,
	const VkDebugUtilsMessengerCallbackDataEXT* data,
	void* user_data
){
	DRIFT_ABORT("%s", data->pMessage);
	return false;
}

static DriftVkBuffer DriftVkCreateBuffer(DriftVkContext* ctx, VkBufferUsageFlags usage, size_t size, const char* name){
	DriftVkBuffer buffer;
	
	VkResult result = vkCreateBuffer(ctx->device, &(VkBufferCreateInfo){
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size, .usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	}, NULL, &buffer.buffer);
	AssertSuccess(result, "Failed to create Vulkan buffer.");
	NameObject(ctx, VK_OBJECT_TYPE_BUFFER, (u64)buffer.buffer, name);
	
	VkMemoryRequirements mem_reqs;
	vkGetBufferMemoryRequirements(ctx->device, buffer.buffer, &mem_reqs);
	
	result = vkAllocateMemory(ctx->device, &(VkMemoryAllocateInfo){
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = mem_reqs.size,
		.memoryTypeIndex = DriftVkFindMemoryType(ctx, mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
	}, NULL, &buffer.memory);
	AssertSuccess(result, "Failed to allocate Vulkan memory.");
	NameObject(ctx, VK_OBJECT_TYPE_DEVICE_MEMORY, (u64)buffer.memory, "%s mem", name);
	
	result = vkBindBufferMemory(ctx->device, buffer.buffer, buffer.memory, 0);
	AssertSuccess(result, "Failed to bind Vulkan memory.");
	result = vkMapMemory(ctx->device, buffer.memory, 0, size, 0, &buffer.ptr);
	AssertSuccess(result, "Failed to map Vulkan memory.");
	
	return buffer;
}

static DriftVkContext* DriftVkCreateContext(void){
	DriftVkContext* ctx = DRIFT_COPY(DriftSystemMem, ((DriftVkContext){}));
	DriftMapInit(&ctx->destructors, DriftSystemMem, "#VKDestructors", 0);
	
	u32 extensions_count;
	bool success = SDL_Vulkan_GetInstanceExtensions(APP->shell_window, &extensions_count, NULL);
	DRIFT_ASSERT_HARD(success, "Failed to recieve Vulkan extensions from SDL");
	
	const char* extensions[extensions_count + 1];
	success = SDL_Vulkan_GetInstanceExtensions(APP->shell_window, &extensions_count, extensions);
	DRIFT_ASSERT_HARD(success, "Failed to recieve Vulkan extensions from SDL");
	
	if(DRIFT_DEBUG) extensions[extensions_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
	
	VkInstanceCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &(VkApplicationInfo){
			.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
			.pApplicationName = "Veridian Expanse", .pEngineName = "Drift",
			.applicationVersion = VK_MAKE_VERSION(0, 0, 1), .apiVersion = VK_API_VERSION_1_0,
		},
		.ppEnabledExtensionNames = extensions, .enabledExtensionCount = extensions_count,
#if DRIFT_VULKAN_VALIDATE
		.ppEnabledLayerNames = VALIDATION_LAYERS, .enabledLayerCount = VALIDATION_LAYERS_COUNT,
#endif
	};
	
	if(DRIFT_VULKAN_VALIDATE) DRIFT_LOG("Vulkan validation enabled.");
	
	VkResult result = vkCreateInstance(&create_info, NULL, &ctx->instance);
	AssertSuccess(result, "Failed to create Vulkan instance.");
	volkLoadInstanceOnly(ctx->instance);
	
#if DRIFT_DEBUG
	PFN_vkCreateDebugUtilsMessengerEXT create_messenger = (void*)vkGetInstanceProcAddr(ctx->instance, "vkCreateDebugUtilsMessengerEXT");
	DRIFT_ASSERT(create_messenger, "vkCreateDebugUtilsMessengerEXT() not found.");
	create_messenger(ctx->instance, &(VkDebugUtilsMessengerCreateInfoEXT){
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT,
		.pfnUserCallback = VulkanErrorCallback,
	}, NULL, &ctx->messenger);
#endif
	
	success = SDL_Vulkan_CreateSurface(APP->shell_window, ctx->instance, &ctx->surface);
	DRIFT_ASSERT(success, "Failed to create SDL Vulkan surfaces.");
	
	u32 device_count = 0;
	vkEnumeratePhysicalDevices(ctx->instance, &device_count, NULL);
	VkPhysicalDevice devices[device_count];
	vkEnumeratePhysicalDevices(ctx->instance, &device_count, devices);
	DRIFT_ASSERT_HARD(device_count > 0, "No suitable Vulkan devices available.");
	
	for(unsigned i = 0; i < device_count; i++){
		VkPhysicalDevice device = devices[i];
		if(is_device_suitable(ctx, device)){
			ctx->physical_device = device;
			break;
		}
	}
	DRIFT_ASSERT_HARD(ctx->physical_device, "Failed to find a suitable Vulkan physical device.");
	
	vkGetPhysicalDeviceProperties(ctx->physical_device, &ctx->physical_properties);
	DRIFT_LOG("Vulkan device: %s (id: %d, type: %d)",
		ctx->physical_properties.deviceName,
		ctx->physical_properties.deviceID,
		ctx->physical_properties.deviceType
	);
	
	ctx->queue_families = find_queue_families(ctx, ctx->physical_device);
	bool same_queue = ctx->queue_families.graphics_idx == ctx->queue_families.present_idx;
	VkDeviceQueueCreateInfo infos[2] = {
		[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		[0].queueFamilyIndex = ctx->queue_families.graphics_idx,
		[0].pQueuePriorities = (float[]){1}, [0].queueCount = 1,
		[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		[1].queueFamilyIndex = ctx->queue_families.present_idx,
		[1].pQueuePriorities = (float[]){1}, [1].queueCount = 1,
	};
	
	result = vkCreateDevice(ctx->physical_device, &(VkDeviceCreateInfo){
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pQueueCreateInfos = infos, .queueCreateInfoCount = (same_queue ? 1 : 2),
		.pEnabledFeatures = &(VkPhysicalDeviceFeatures){},
		.ppEnabledExtensionNames = REQUIRED_EXTENSIONS, .enabledExtensionCount = REQUIRED_EXTENSIONS_COUNT,
#if DRIFT_VULKAN_VALIDATE
		.ppEnabledLayerNames = VALIDATION_LAYERS, .enabledLayerCount = VALIDATION_LAYERS_COUNT,
#endif
	}, NULL, &ctx->device);
	AssertSuccess(result, "Failed to create Vulkan device.");
	volkLoadDevice(ctx->device);
	
	vkGetDeviceQueue(ctx->device, ctx->queue_families.graphics_idx, 0, &ctx->graphics_queue);
	vkGetDeviceQueue(ctx->device, ctx->queue_families.present_idx, 0, &ctx->present_queue);
	
	result = vkCreateCommandPool(ctx->device, &(VkCommandPoolCreateInfo){
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.queueFamilyIndex = ctx->queue_families.graphics_idx,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
	}, NULL, &ctx->command_pool);
	AssertSuccess(result, "Failed to create Vulkan command buffer.");
	
	VkBufferUsageFlags staging_buffer_usage_flags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	ctx->staging.buffer = DriftVkCreateBuffer(ctx, staging_buffer_usage_flags, DRIFT_VK_STAGING_BUFFER_SIZE, "StagingBuffer");
	for(uint i = 0; i < DRIFT_VK_STAGING_JOB_COUNT; i++){
		result = vkCreateFence(ctx->device, &(VkFenceCreateInfo){
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.flags = VK_FENCE_CREATE_SIGNALED_BIT,
		}, NULL, &ctx->staging.jobs[i].fence);
		AssertSuccess(result, "Failed to create Vulkan fence.");
		
		result = vkAllocateCommandBuffers(ctx->device, &(VkCommandBufferAllocateInfo){
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = ctx->command_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1,
		}, &ctx->staging.jobs[i].command_buffer);
		AssertSuccess(result, "Failed to allocate Vulkan command buffers.");
	}
	
	for(uint i = 0; i < DRIFT_VK_RENDERER_COUNT; i++){
		DriftVkRenderer* renderer = DriftAlloc(DriftSystemMem, sizeof(*renderer));
		DriftGfxRendererInit(&renderer->base, (DriftGfxVTable){
			.bind_target = DriftVkRendererBindTarget,
			.set_scissor = DriftVkCommandSetScissor,
			.bind_pipeline = DriftVkRendererBindPipeline,
			.draw_indexed = DriftVkRendererDrawIndexed,
		});
		renderer->ctx = ctx;
		
		size_t buffer_size = DRIFT_GFX_VERTEX_BUFFER_SIZE + DRIFT_GFX_INDEX_BUFFER_SIZE + DRIFT_GFX_UNIFORM_BUFFER_SIZE;
		VkBufferUsageFlags render_buffer_usage_flags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		renderer->buffer = DriftVkCreateBuffer(ctx, render_buffer_usage_flags, buffer_size, "RendererBuffer");
		renderer->base.ptr.vertex = renderer->buffer.ptr;
		renderer->base.ptr.index = renderer->buffer.ptr + DRIFT_GFX_VERTEX_BUFFER_SIZE;
		renderer->base.ptr.uniform = renderer->buffer.ptr + DRIFT_GFX_VERTEX_BUFFER_SIZE + DRIFT_GFX_INDEX_BUFFER_SIZE;
		renderer->base.uniform_alignment = ctx->physical_properties.limits.minUniformBufferOffsetAlignment;
		
		uint draw_count = 1024;
		VkDescriptorPoolSize pool_size[] = {
			{.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = draw_count*DRIFT_GFX_UNIFORM_BINDING_COUNT},
			{.type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = draw_count*DRIFT_GFX_SAMPLER_BINDING_COUNT},
			{.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = draw_count*DRIFT_GFX_TEXTURE_BINDING_COUNT},
		};
		
		VkResult result = vkCreateDescriptorPool(ctx->device, &(VkDescriptorPoolCreateInfo){
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.maxSets = draw_count, .pPoolSizes = pool_size, .poolSizeCount = 3,
		}, NULL, &renderer->descriptor_pool);
		AssertSuccess(result, "Failed to create Vulkan descriptor pool.");
		
		result = vkAllocateCommandBuffers(ctx->device, &(VkCommandBufferAllocateInfo){
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = ctx->command_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1,
		}, &renderer->command_buffer);
		AssertSuccess(result, "Failed to allocate Vulkan command buffer.");
		
		result = vkCreateFence(ctx->device, &(VkFenceCreateInfo){
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.flags = VK_FENCE_CREATE_SIGNALED_BIT,
		}, NULL, &renderer->command_fence);
		AssertSuccess(result, "Failed to create Vulkan fence.");
		
		ctx->renderers[i] = renderer;
	}
	
	return ctx;
}

static void DriftVkRendererExecute(DriftVkRenderer* renderer){
	DriftVkContext* ctx = renderer->ctx;
	VkResult result;
	
	result = vkResetDescriptorPool(ctx->device, renderer->descriptor_pool, 0);
	AssertSuccess(result, "Failed to reset Vulkan descriptor pool.");
	result = vkBeginCommandBuffer(renderer->command_buffer, &(VkCommandBufferBeginInfo){
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	});
	AssertSuccess(result, "Failed to begin Vulkan command buffer.");
	
	// Set initial blend color.
	vkCmdSetBlendConstants(renderer->command_buffer, (float[]){0, 0, 0, 0});
	
	renderer->current_pass = 0;
	DriftRendererExecuteCommands(&renderer->base);
	
	DRIFT_ASSERT(renderer->current_pass, "DriftVkRendererExecute() no target bound.");
	vkCmdEndRenderPass(renderer->command_buffer);
	result = vkEndCommandBuffer(renderer->command_buffer);
	AssertSuccess(result, "Failed to end Vulkan command buffer.");
	
	result = vkWaitForFences(ctx->device, 1, &ctx->swap_chain.fence, true, UINT64_MAX);
	AssertSuccess(result, "Failed to wait for Vulkan fence.");
	result = vkResetFences(ctx->device, 1, &ctx->swap_chain.fence);
	AssertSuccess(result, "Failed to reset Vulkan fence.");
	
	result = vkQueueSubmit(ctx->graphics_queue, 1, &(VkSubmitInfo){
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pCommandBuffers = &renderer->command_buffer, .commandBufferCount = 1,
		.pWaitDstStageMask = (VkPipelineStageFlags[]){VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT},
		.pSignalSemaphores = ctx->swap_chain.semaphores + ctx->swap_chain.image_index, .signalSemaphoreCount = 1,
	}, renderer->command_fence);
	AssertSuccess(result, "Failed to submit Vulkan queue.");
}

static const VkFilter DriftGfxFilterToVk[_DRIFT_GFX_FILTER_COUNT] = {
	[DRIFT_GFX_FILTER_NEAREST] = VK_FILTER_NEAREST,
	[DRIFT_GFX_FILTER_LINEAR] = VK_FILTER_LINEAR,
};

static const VkSamplerMipmapMode DriftGfxMipFilterToVk[_DRIFT_GFX_MIP_FILTER_COUNT] = {
	[DRIFT_GFX_MIP_FILTER_NONE] = VK_SAMPLER_MIPMAP_MODE_NEAREST,
	[DRIFT_GFX_MIP_FILTER_NEAREST] = VK_SAMPLER_MIPMAP_MODE_NEAREST,
	[DRIFT_GFX_MIP_FILTER_LINEAR] = VK_SAMPLER_MIPMAP_MODE_LINEAR,
};

static const VkSamplerAddressMode DriftGfxAddressModeToVk[_DRIFT_GFX_ADDRESS_MODE_COUNT] = {
	[DRIFT_GFX_ADDRESS_MODE_CLAMP_TO_EDGE] = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
	[DRIFT_GFX_ADDRESS_MODE_CLAMP_TO_BORDER] = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
	[DRIFT_GFX_ADDRESS_MODE_REPEAT] = VK_SAMPLER_ADDRESS_MODE_REPEAT,
	[DRIFT_GFX_ADDRESS_MODE_MIRRORED_REPEAT] = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
};

static void DriftVkSamplerFree(const DriftGfxDriver* driver, void* obj){
	DriftVkContext* ctx = driver->ctx;
	DriftVkSampler* sampler = obj;
	vkDestroySampler(ctx->device, sampler->sampler, NULL);
}

static DriftGfxSampler* DriftVkSamplerNew(const DriftGfxDriver* driver, DriftGfxSamplerOptions options){
	DriftVkContext* ctx = driver->ctx;
	DriftVkSampler* sampler = DRIFT_COPY(DriftSystemMem, ((DriftVkSampler){}));
	DriftAssertGfxThread();
	
	vkCreateSampler(ctx->device, &(VkSamplerCreateInfo){
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.minFilter = DriftGfxFilterToVk[options.min_filter],
		.magFilter = DriftGfxFilterToVk[options.mag_filter],
		.mipmapMode = DriftGfxMipFilterToVk[options.mip_filter],
		.addressModeU = DriftGfxAddressModeToVk[options.address_x],
		.addressModeV = DriftGfxAddressModeToVk[options.address_y],
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
	}, NULL, &sampler->sampler);
	
	DriftMapInsert(&ctx->destructors, (uintptr_t)sampler, (uintptr_t)DriftVkSamplerFree);
	return &sampler->base;
}

static DriftVkStagingJob* DriftVkAquireStagingJob(DriftVkContext* ctx, size_t job_size){
	DRIFT_ASSERT_HARD(job_size <= DRIFT_VK_STAGING_BUFFER_SIZE, "Data size exceeds Vulkan staging buffer size!");
	
	TracyCZoneN(ZONE_STAGING, "Staging", true);
	// Check if there is enough space remaining before wraparound.
	size_t remaining = DRIFT_VK_STAGING_BUFFER_SIZE - (ctx->staging.buffer_write % DRIFT_VK_STAGING_BUFFER_SIZE);
	if(remaining < job_size){
		// Jump forward to the wrap point.
		ctx->staging.buffer_write += remaining;
		// Add the space to the most recent job.
		uint last_idx = (ctx->staging.job_head - 1) % DRIFT_VK_STAGING_JOB_COUNT;
		ctx->staging.jobs[last_idx].advance = ctx->staging.buffer_write;
	}
	
	size_t buffer_offset = ctx->staging.buffer_write % DRIFT_VK_STAGING_BUFFER_SIZE;
	ctx->staging.buffer_write += job_size;
	
	// Check for finished jobs if not enough buffer memory is available.
	while(ctx->staging.buffer_write - ctx->staging.buffer_read > DRIFT_VK_STAGING_BUFFER_SIZE){
		uint wait_idx = ctx->staging.job_tail++ & (DRIFT_VK_STAGING_JOB_COUNT - 1);
		VkResult result = vkWaitForFences(ctx->device, 1, &ctx->staging.jobs[wait_idx].fence, true, UINT64_MAX);
		AssertSuccess(result, "Failed to wait on Vulkan fence.");
		ctx->staging.buffer_read = ctx->staging.jobs[wait_idx].advance;
	}
	
	// Aquire a job.
	uint job_idx = ctx->staging.job_head++ % DRIFT_VK_STAGING_JOB_COUNT;
	DriftVkStagingJob* job = ctx->staging.jobs + job_idx;
	VkResult result = vkWaitForFences(ctx->device, 1, &job->fence, true, UINT64_MAX);
	AssertSuccess(result, "Failed to wait on Vulkan fence.");
	result = vkResetFences(ctx->device, 1, &job->fence);
	AssertSuccess(result, "Failed to reset Vulkan fence.");
	
	// Prepare command buffer.
	result = vkBeginCommandBuffer(job->command_buffer, &(VkCommandBufferBeginInfo){
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	});
	AssertSuccess(result, "Failed to begin Vulkan command buffer");
	TracyCZoneEnd(ZONE_STAGING);
	
	job->offset = buffer_offset;
	job->advance = ctx->staging.buffer_write;
	return job;
}

static void TransitionImage(VkCommandBuffer command_buffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, uint layer0, uint layer_count){
	vkCmdPipelineBarrier(command_buffer,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		0, NULL, 0, NULL, 1, &(VkImageMemoryBarrier){
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = image, .oldLayout = oldLayout, .newLayout = newLayout,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = layer0, .layerCount = layer_count},
		}
	);
}

struct {
	VkFormat format;
	uint bytes_per_pixel;
} DriftVkFormatMap[_DRIFT_GFX_TEXTURE_FORMAT_COUNT] = {
	[DRIFT_GFX_TEXTURE_FORMAT_RGBA8] = {VK_FORMAT_R8G8B8A8_UNORM, 4},
	[DRIFT_GFX_TEXTURE_FORMAT_RGBA16F] = {VK_FORMAT_R16G16B16A16_SFLOAT, 8},
};

static void DriftVkTextureFree(const DriftGfxDriver* driver, void* obj){
	DriftVkContext* ctx = driver->ctx;
	DriftVkTexture* texture = obj;
	vkDestroyImageView(ctx->device, texture->view, NULL);
	vkDestroyImage(ctx->device, texture->image, NULL);
	vkFreeMemory(ctx->device, texture->memory, NULL);
}

static DriftGfxTexture* DriftVkTextureNew(const DriftGfxDriver* driver, uint width, uint height, DriftGfxTextureOptions options){
	DriftVkContext* ctx = driver->ctx;
	DriftAssertGfxThread();
	
	VkFormat format = DriftVkFormatMap[options.format].format;
	DRIFT_ASSERT(format, "Unhandled texture format.");
	if(options.layers == 0) options.layers = 1;
	
	static const VkImageType image_types[] = {
		[DRIFT_GFX_TEXTURE_2D] = VK_IMAGE_TYPE_2D,
		[DRIFT_GFX_TEXTURE_2D_ARRAY] = VK_IMAGE_TYPE_2D,
	};
	
	DriftVkTexture *texture = DRIFT_COPY(DriftSystemMem, ((DriftVkTexture){
		.base = {.options = options, .width = width, .height = height}
	}));
	
	VkResult result = vkCreateImage(ctx->device, &(VkImageCreateInfo){
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = image_types[options.type], .extent = {width, height, 1},
		.mipLevels = 1, .arrayLayers = options.layers, .format = format,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | (options.render_target ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : VK_IMAGE_USAGE_TRANSFER_DST_BIT),
		.tiling = VK_IMAGE_TILING_OPTIMAL, .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.samples = VK_SAMPLE_COUNT_1_BIT, .flags = 0,
	}, NULL, &texture->image);
	AssertSuccess(result, "Failed to create Vulkan image.");
	NameObject(ctx, VK_OBJECT_TYPE_IMAGE, (u64)texture->image, "%s image", options.name);
	
	VkMemoryRequirements memory_requirements;
	vkGetImageMemoryRequirements(ctx->device, texture->image, &memory_requirements);
	
	result = vkAllocateMemory(ctx->device, &(VkMemoryAllocateInfo){
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = memory_requirements.size,
		.memoryTypeIndex = DriftVkFindMemoryType(ctx, memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
	}, NULL, &texture->memory);
	AssertSuccess(result, "Failed to allocate memory for Vulkan image");
	result = vkBindImageMemory(ctx->device, texture->image, texture->memory, 0);
	AssertSuccess(result, "Failed to bind Vulkan image memory.");
	NameObject(ctx, VK_OBJECT_TYPE_DEVICE_MEMORY, (u64)texture->memory, "%s mem", options.name);
	
	static const VkImageViewType view_types[] = {
		[DRIFT_GFX_TEXTURE_2D] = VK_IMAGE_VIEW_TYPE_2D,
		[DRIFT_GFX_TEXTURE_2D_ARRAY] = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
	};
	
	result = vkCreateImageView(ctx->device, &(VkImageViewCreateInfo){
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = texture->image, .viewType = view_types[options.type], .format = format,
		.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = options.layers},
	}, NULL, &texture->view);
	AssertSuccess(result, "Failed to create Vulkan image view");
	NameObject(ctx, VK_OBJECT_TYPE_IMAGE_VIEW, (u64)texture->view, "%s view", options.name);
	
	// TODO wasn't there a simpler way to set initial layout than a barrier?
	// TODO can I clear the pixels with a transfer job of some sort?
	DriftVkStagingJob* job = DriftVkAquireStagingJob(ctx, 0);
	TransitionImage(job->command_buffer, texture->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, options.layers);
	result = vkEndCommandBuffer(job->command_buffer);
	AssertSuccess(result, "Failed to end Vulkan command buffer.");
	
	result = vkQueueSubmit(ctx->graphics_queue, 1, &(VkSubmitInfo){
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pCommandBuffers = &job->command_buffer, .commandBufferCount = 1,
	}, job->fence);
	AssertSuccess(result, "Failed to commit Vulkan command buffer.");
	
	DriftMapInsert(&ctx->destructors, (uintptr_t)texture, (uintptr_t)DriftVkTextureFree);
	return &texture->base;
}

void DriftVkLoadTextureLayer(const DriftGfxDriver* driver, DriftGfxTexture* texture, uint layer, const void* pixels){
	DriftVkContext* ctx = driver->ctx;
	DriftVkTexture* _texture = (DriftVkTexture*)texture;
	DriftGfxTextureOptions options = texture->options;
	DriftAssertGfxThread();
	
	size_t job_size = DriftVkFormatMap[options.format].bytes_per_pixel*texture->width*texture->height;
	DriftVkStagingJob* job = DriftVkAquireStagingJob(ctx, job_size);
	memcpy(ctx->staging.buffer.ptr + job->offset, pixels, job_size);
	
	// Setup command buffer to copy data to the texture.
	TransitionImage(job->command_buffer, _texture->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, layer, 1);
	vkCmdCopyBufferToImage(job->command_buffer, ctx->staging.buffer.buffer, _texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &(VkBufferImageCopy){
		.bufferOffset = job->offset, .imageExtent = {texture->width, texture->height, 1},
		.imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = layer, .layerCount = 1},
	});
	
	TransitionImage(job->command_buffer, _texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, layer, 1);
	VkResult result = vkEndCommandBuffer(job->command_buffer);
	AssertSuccess(result, "Failed to end Vulkan command buffer.");
	
	// TODO bulk uploads? This submit call is over half the cost!!
	result = vkQueueSubmit(ctx->graphics_queue, 1, &(VkSubmitInfo){
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pCommandBuffers = &job->command_buffer, .commandBufferCount = 1,
	}, job->fence);
	AssertSuccess(result, "Failed to commit Vulkan command buffer.");
}

static VkShaderModule DriftVkShaderModule(DriftVkContext* ctx, const char* shader_name, const char* extension){
	u8 buffer[64*1024];
	DriftMem* mem = DriftLinearMemMake(buffer, sizeof(buffer), "Shader Mem");
	DriftData spirv = DriftAssetLoadf(mem, "shaders/%s%s", shader_name, extension);
	
	VkShaderModule module;
	VkResult result = vkCreateShaderModule(ctx->device, &(VkShaderModuleCreateInfo){
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.pCode = spirv.ptr, .codeSize = spirv.size,
	}, NULL, &module);
	AssertSuccess(result, "Failed to create Vulkan shader module.");
	
	return module;
}

static void DriftVkShaderFree(const DriftGfxDriver* driver, void* obj){
	DriftVkContext* ctx = driver->ctx;
	DriftVkShader* shader = obj;
	vkDestroyShaderModule(ctx->device, shader->vshader, NULL);
	vkDestroyShaderModule(ctx->device, shader->fshader, NULL);
}

static DriftGfxShader* DriftVkShaderLoad(const DriftGfxDriver* driver, const char* name, const DriftGfxShaderDesc* desc){
	DriftVkContext* ctx = driver->ctx;
	DriftAssertGfxThread();
	
	DriftVkShader* shader = DRIFT_COPY(DriftSystemMem, ((DriftVkShader){
		.base.desc = desc,
		.base.name = name,
		.vshader = DriftVkShaderModule(ctx, name, ".vert.spv"),
		.fshader = DriftVkShaderModule(ctx, name, ".frag.spv"),
	}));
	
	DriftMapInsert(&ctx->destructors, (uintptr_t)shader, (uintptr_t)DriftVkShaderFree);
	return &shader->base;
}


VkFormat DriftGfxTypeMap[_DRIFT_GFX_TYPE_COUNT] = {
	[DRIFT_GFX_TYPE_U8] = VK_FORMAT_R8_UINT,
	[DRIFT_GFX_TYPE_U8_2] = VK_FORMAT_R8G8_UINT,
	[DRIFT_GFX_TYPE_U8_4] = VK_FORMAT_R8G8B8A8_UINT,
	[DRIFT_GFX_TYPE_UNORM8_2] = VK_FORMAT_R8G8_UNORM,
	[DRIFT_GFX_TYPE_UNORM8_4] = VK_FORMAT_R8G8B8A8_UNORM,
	[DRIFT_GFX_TYPE_U16] = VK_FORMAT_R16_UINT,
	[DRIFT_GFX_TYPE_FLOAT32] = VK_FORMAT_R32_SFLOAT,
	[DRIFT_GFX_TYPE_FLOAT32_2] = VK_FORMAT_R32G32_SFLOAT,
	[DRIFT_GFX_TYPE_FLOAT32_3] = VK_FORMAT_R32G32B32_SFLOAT,
	[DRIFT_GFX_TYPE_FLOAT32_4] = VK_FORMAT_R32G32B32A32_SFLOAT,
};

static const VkBlendFactor DriftGfxBlendFactorToVk[] = {
	[DRIFT_GFX_BLEND_FACTOR_ZERO] = VK_BLEND_FACTOR_ZERO,
	[DRIFT_GFX_BLEND_FACTOR_ONE] = VK_BLEND_FACTOR_ONE,
	[DRIFT_GFX_BLEND_FACTOR_SRC_COLOR] = VK_BLEND_FACTOR_SRC_COLOR,
	[DRIFT_GFX_BLEND_FACTOR_ONE_MINUS_SRC_COLOR] = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
	[DRIFT_GFX_BLEND_FACTOR_DST_COLOR] = VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
	[DRIFT_GFX_BLEND_FACTOR_ONE_MINUS_DST_COLOR] = VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
	[DRIFT_GFX_BLEND_FACTOR_SRC_ALPHA] = VK_BLEND_FACTOR_SRC_ALPHA,
	[DRIFT_GFX_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA] = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
	[DRIFT_GFX_BLEND_FACTOR_DST_ALPHA] = VK_BLEND_FACTOR_DST_ALPHA,
	[DRIFT_GFX_BLEND_FACTOR_ONE_MINUS_DST_ALPHA] = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
	[DRIFT_GFX_BLEND_FACTOR_SRC_ALPHA_SATURATE] = VK_BLEND_FACTOR_SRC_ALPHA_SATURATE,
	[DRIFT_GFX_BLEND_FACTOR_CONSTANT_COLOR] = VK_BLEND_FACTOR_CONSTANT_COLOR,
};

static const VkBlendOp DriftGfxBlendOpToVk[_DRIFT_GFX_BLEND_OP_COUNT] = {
	[DRIFT_GFX_BLEND_OP_ADD] = VK_BLEND_OP_ADD,
	[DRIFT_GFX_BLEND_OP_SUBTRACT] = VK_BLEND_OP_SUBTRACT,
	[DRIFT_GFX_BLEND_OP_REVERSE_SUBTRACT] = VK_BLEND_OP_REVERSE_SUBTRACT,
	[DRIFT_GFX_BLEND_OP_MIN] = VK_BLEND_OP_MIN,
	[DRIFT_GFX_BLEND_OP_MAX] = VK_BLEND_OP_MAX,
};

static void DriftVkPipelineFree(const DriftGfxDriver* driver, void* obj){
	DriftVkContext* ctx = driver->ctx;
	DriftVkPipeline* pipeline = obj;
	vkDestroyPipeline(ctx->device, pipeline->pipeline, NULL);
	vkDestroyPipelineLayout(ctx->device, pipeline->pipeline_layout, NULL);
	vkDestroyDescriptorSetLayout(ctx->device, pipeline->descriptor_set_layout, NULL);
}

static void AddLayoutBindings(VkDescriptorSetLayoutBinding bindings[], uint* count, const char* const resources[], uint resource_count, uint offset, VkDescriptorType type){
	for(uint i = 0; i < resource_count; i++){
		// Add bindings for all declared resources.
		if(resources[i]) bindings[(*count)++] = (VkDescriptorSetLayoutBinding){.binding = i + offset, .descriptorCount = 1, .descriptorType = type, .stageFlags = VK_SHADER_STAGE_ALL};
	}
}

static DriftGfxPipeline* DriftVkPipelineNew(const DriftGfxDriver* driver, DriftGfxPipelineOptions options){
	DriftVkContext* ctx = driver->ctx;
	DriftAssertGfxThread();
	
	DriftVkPipeline* pipeline = DRIFT_COPY(DriftSystemMem, ((DriftVkPipeline){.base = {.options = options}}));
	DriftVkShader* _shader = (DriftVkShader*)options.shader;
	DriftVkRenderTarget* _target = (DriftVkRenderTarget*)options.target;
	
	VkPipelineShaderStageCreateInfo shader_stages[] = {
		[0] = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT, .module = _shader->vshader, .pName = "VShader",
		},
		[1] = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = _shader->fshader, .pName = "FShader",
		}
	};
	
	const DriftGfxShaderDesc* desc = options.shader->desc;
	VkVertexInputBindingDescription vk_bindings[] = {
		{.binding = 0, .stride = desc->vertex_stride, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX},
		{.binding = 1, .stride = desc->instance_stride, .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE},
	};
	
	uint attrib_count = 0;
	VkVertexInputAttributeDescription vk_attribs[DRIFT_GFX_VERTEX_ATTRIB_COUNT];
	const DriftGfxVertexAttrib* attribs = desc->vertex;
	for(int i = 0; i < DRIFT_GFX_VERTEX_ATTRIB_COUNT; i++){
		DriftGfxVertexAttrib attr = attribs[i];
		if(attr.type != _DRIFT_GFX_TYPE_NONE){
			DRIFT_ASSERT(DriftGfxTypeMap[attr.type], "Invalid format.");
			vk_attribs[attrib_count++] = (VkVertexInputAttributeDescription){
				.location = i, .binding = !!attr.instanced, .format = DriftGfxTypeMap[attr.type], .offset = attr.offset
			};
		}
	}
	
	VkPipelineVertexInputStateCreateInfo vertex_input_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.pVertexBindingDescriptions = vk_bindings, .vertexBindingDescriptionCount = 2,
		.pVertexAttributeDescriptions = vk_attribs, .vertexAttributeDescriptionCount = attrib_count,
	};
	
	VkPipelineInputAssemblyStateCreateInfo input_assembly_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	};
	
	uint binding_count = 0;
	VkDescriptorSetLayoutBinding layout_bindings[DRIFT_GFX_UNIFORM_BINDING_COUNT + DRIFT_GFX_SAMPLER_BINDING_COUNT + DRIFT_GFX_TEXTURE_BINDING_COUNT] = {};
	AddLayoutBindings(layout_bindings, &binding_count, options.shader->desc->uniform, DRIFT_GFX_UNIFORM_BINDING_COUNT, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	AddLayoutBindings(layout_bindings, &binding_count, options.shader->desc->sampler, DRIFT_GFX_SAMPLER_BINDING_COUNT, 4, VK_DESCRIPTOR_TYPE_SAMPLER);
	AddLayoutBindings(layout_bindings, &binding_count, options.shader->desc->texture, DRIFT_GFX_TEXTURE_BINDING_COUNT, 8, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
	
	VkResult result = vkCreateDescriptorSetLayout(ctx->device, &(VkDescriptorSetLayoutCreateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pBindings = layout_bindings, .bindingCount = binding_count,
	}, NULL, &pipeline->descriptor_set_layout);
	AssertSuccess(result, "Failed to create Vulkan descriptor set layout.");
	NameObject(ctx, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (u64)pipeline->descriptor_set_layout, "%s descriptor layout", options.shader->name);
	
	VkExtent2D size = ctx->swap_chain.extent;
	if(_target) size = (VkExtent2D){(uint)_target->base.framebuffer_size.x, (uint)_target->base.framebuffer_size.y};

	VkPipelineDynamicStateCreateInfo dynamic_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.pDynamicStates = (VkDynamicState[]){VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_BLEND_CONSTANTS}, .dynamicStateCount = 3,
	};
	
	VkPipelineViewportStateCreateInfo viewport_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1, .scissorCount = 1,
	};
	
	static const VkCullModeFlagBits CullModeMap[_DRIFT_GFX_CULL_MODE_COUNT] = {
		[DRIFT_GFX_CULL_MODE_NONE] = VK_CULL_MODE_NONE,
		[DRIFT_GFX_CULL_MODE_FRONT] = VK_CULL_MODE_FRONT_BIT,
		[DRIFT_GFX_CULL_MODE_BACK] = VK_CULL_MODE_BACK_BIT,
	};
	
	VkPipelineRasterizationStateCreateInfo raster_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL, .lineWidth = 1,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .cullMode = CullModeMap[options.cull_mode],
	};
	
	VkPipelineColorBlendAttachmentState blend_state = {
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
		.blendEnable = !!options.blend,
	};
	
	if(options.blend){
		blend_state.colorBlendOp = DriftGfxBlendOpToVk[options.blend->color_op];
		blend_state.srcColorBlendFactor = DriftGfxBlendFactorToVk[options.blend->color_src_factor];
		blend_state.dstColorBlendFactor = DriftGfxBlendFactorToVk[options.blend->color_dst_factor];
		blend_state.alphaBlendOp = DriftGfxBlendOpToVk[options.blend->alpha_op];
		blend_state.srcAlphaBlendFactor = DriftGfxBlendFactorToVk[options.blend->alpha_src_factor];
		blend_state.dstAlphaBlendFactor = DriftGfxBlendFactorToVk[options.blend->alpha_dst_factor];
	}
	
	VkPipelineColorBlendAttachmentState blend_states[DRIFT_GFX_RENDER_TARGET_COUNT];
	for(uint i = 0; i < DRIFT_GFX_RENDER_TARGET_COUNT; i++) blend_states[i] = blend_state;
	
	VkPipelineColorBlendStateCreateInfo blend_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.pAttachments = blend_states, .attachmentCount = _target ? _target->attachment_count : 1,
	};
	
	VkPipelineMultisampleStateCreateInfo msample_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT, .minSampleShading = 1,
	};
	
	result = vkCreatePipelineLayout(ctx->device, &(VkPipelineLayoutCreateInfo){
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.pSetLayouts = &pipeline->descriptor_set_layout, .setLayoutCount = 1,
	}, NULL, &pipeline->pipeline_layout);
	AssertSuccess(result, "Failed to create Vulkan pipeline layout.");
	NameObject(ctx, VK_OBJECT_TYPE_PIPELINE_LAYOUT, (u64)pipeline->pipeline_layout, "%s pipeline layout", options.shader->name);
	
	result = vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1, &(VkGraphicsPipelineCreateInfo){
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pStages = shader_stages, .stageCount = 2,
		.pInputAssemblyState = &input_assembly_info,
		.pVertexInputState = &vertex_input_info,
		.layout = pipeline->pipeline_layout,
		.pDynamicState = &dynamic_info,
		.pViewportState = &viewport_info,
		.pRasterizationState = &raster_info,
		.pColorBlendState = &blend_info,
		.pMultisampleState = &msample_info,
		.renderPass = _target ? _target->render_pass : ctx->render_pass,
	}, NULL, (VkPipeline*)&pipeline->pipeline);
	AssertSuccess(result, "Failed to create Vulkan pipeline.");
	NameObject(ctx, VK_OBJECT_TYPE_PIPELINE, (u64)pipeline->pipeline, "%s pipeline", options.shader->name);

	DriftMapInsert(&ctx->destructors, (uintptr_t)pipeline, (uintptr_t)DriftVkPipelineFree);
	return &pipeline->base;
}

static void DriftVkRenderTargetFree(const DriftGfxDriver* driver, void* obj){
	DriftVkContext* ctx = driver->ctx;
	DriftVkRenderTarget* target = obj;
	vkDestroyFramebuffer(ctx->device, target->framebuffer, NULL);
	vkDestroyRenderPass(ctx->device, target->render_pass, NULL);
	for(uint i = 0; i < DRIFT_GFX_RENDER_TARGET_COUNT; i++){
		if(target->views[i]) vkDestroyImageView(ctx->device, target->views[i], NULL);
	}
}

static VkAttachmentLoadOp DriftGfxLoadOpMap[] = {
	[DRIFT_GFX_LOAD_ACTION_DONT_CARE] = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
	[DRIFT_GFX_LOAD_ACTION_CLEAR] = VK_ATTACHMENT_LOAD_OP_CLEAR,
	[DRIFT_GFX_LOAD_ACTION_LOAD] = VK_ATTACHMENT_LOAD_OP_LOAD,
};

static VkAttachmentStoreOp DriftGfxStoreOpMap[_DRIFT_GFX_STORE_ACTION_COUNT] = {
	[DRIFT_GFX_STORE_ACTION_DONT_CARE] = VK_ATTACHMENT_STORE_OP_DONT_CARE,
	[DRIFT_GFX_STORE_ACTION_STORE] = VK_ATTACHMENT_STORE_OP_STORE,
};

static DriftGfxRenderTarget* DriftVkRenderTargetNew(const DriftGfxDriver* driver, DriftGfxRenderTargetOptions options){
	DriftVkContext* ctx = driver->ctx;
	DriftAssertGfxThread();
	
	uint width = options.bindings[0].texture->width;
	uint height = options.bindings[0].texture->height;
	DriftVkRenderTarget *target = DRIFT_COPY(DriftSystemMem, ((DriftVkRenderTarget){
		.base = {.load = options.load, .store = options.store, .framebuffer_size = {width, height}}
	}));
	
	VkAttachmentDescription attachments[DRIFT_GFX_RENDER_TARGET_COUNT] = {};
	VkAttachmentReference color_refs[DRIFT_GFX_RENDER_TARGET_COUNT];
	for(uint i = 0; i < DRIFT_GFX_RENDER_TARGET_COUNT; i++){
		DriftVkTexture* texture = (DriftVkTexture*)options.bindings[i].texture;
		if(texture){
			DRIFT_ASSERT(texture->base.width == width && texture->base.height == height, "Render target dimensions must match.");
			VkFormat format = DriftVkFormatMap[texture->base.options.format].format;
			
			attachments[i] = (VkAttachmentDescription){
				.format = format, .samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = DriftGfxLoadOpMap[options.load], .storeOp = DriftGfxStoreOpMap[options.store],
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
			color_refs[i] = (VkAttachmentReference){.attachment = i, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
			
			VkResult result = vkCreateImageView(ctx->device, &(VkImageViewCreateInfo){
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.image = texture->image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = format,
				.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = options.bindings[i].layer, .layerCount = 1},
			}, NULL, target->views + target->attachment_count);
			AssertSuccess(result, "Failed to create Vulkan image view for framebuffer");
			NameObject(ctx, VK_OBJECT_TYPE_IMAGE_VIEW, (u64)target->views[target->attachment_count],  "%s view %d", options.name, target->attachment_count);
			target->attachment_count++;
		}
	}
	
	VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .pColorAttachments = color_refs, .colorAttachmentCount = target->attachment_count,};
	VkSubpassDependency dependencies[] = {
		[0] = {
			.srcSubpass = VK_SUBPASS_EXTERNAL, .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.dstSubpass = 0, .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
		},
		[1] = {
			.srcSubpass = 0, .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstSubpass = VK_SUBPASS_EXTERNAL, .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
		}
	};
	
	VkResult result = vkCreateRenderPass(ctx->device, &(VkRenderPassCreateInfo){
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.pAttachments = attachments, .attachmentCount = target->attachment_count,
		.pSubpasses = &subpass, .subpassCount = 1,
		.pDependencies = dependencies, .dependencyCount = 2,
	}, NULL, &target->render_pass);
	AssertSuccess(result, "Failed to create Vulkan render pass for render target.");
	NameObject(ctx, VK_OBJECT_TYPE_RENDER_PASS, (u64)target->render_pass, "%s render pass", options.name);
	
	result = vkCreateFramebuffer(ctx->device, &(VkFramebufferCreateInfo){
		.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		.renderPass = target->render_pass,
		.pAttachments = target->views, .attachmentCount = target->attachment_count,
		.width = width, .height = height, .layers = 1,
	}, NULL, &target->framebuffer);
	NameObject(ctx, VK_OBJECT_TYPE_FRAMEBUFFER, (u64)target->framebuffer, "%s framebuffer", options.name);
	
	DriftMapInsert(&ctx->destructors, (uintptr_t)target, (uintptr_t)DriftVkRenderTargetFree);
	return &target->base;
}

static void DriftVkFreeObjects(const DriftGfxDriver* driver, void* objects[], uint count){
	DriftVkContext* ctx = driver->ctx;
	DriftAssertGfxThread();
	
	vkDeviceWaitIdle(ctx->device);
	DriftGfxFreeObjects(driver, &ctx->destructors, objects, count);
}

static void DriftVkFreeAll(const DriftGfxDriver* driver){
	DriftVkContext* ctx = driver->ctx;
	vkDeviceWaitIdle(ctx->device);
	DriftGfxFreeAll(driver, &ctx->destructors);
}

static void DriftVKRecreateSwapChain(DriftVkContext* ctx, DriftVec2 fb_extent){
	vkDeviceWaitIdle(ctx->device);
	DriftVkDestroySwapChain(ctx);
	DriftVkCreateSwapChain(ctx, fb_extent);
}

static VkExtent2D DriftVkAquireImage(DriftVkContext* ctx, DriftVec2 fb_extent){
	DriftVkSwapChain* swap = &ctx->swap_chain;
	
	// Check if we have been resized first.
	if(swap->extent.width != fb_extent.x || swap->extent.height != fb_extent.y) DriftVKRecreateSwapChain(ctx, fb_extent);
	
	// Aquire and handle recreation if necessary.
	VkResult result = vkAcquireNextImageKHR(ctx->device, swap->swap_chain, UINT64_MAX, 0, swap->fence, &swap->image_index);
	if(result == VK_ERROR_OUT_OF_DATE_KHR){
		DriftVKRecreateSwapChain(ctx, fb_extent);
		return DriftVkAquireImage(ctx, fb_extent);
	} else if(result != VK_SUBOPTIMAL_KHR){
		AssertSuccess(result, "Failed to aquire Vulkan swapchain image.");
	}
	
	return ctx->swap_chain.extent;
}

static DriftVec2 drawable_size(void){
	int w, h;
	SDL_Vulkan_GetDrawableSize(APP->shell_window, &w, &h);
	return (DriftVec2){w, h};
}

void* DriftShellSDLVk(DriftShellEvent event, void* shell_value){
	DriftVkContext* ctx = APP->shell_context;
	
	switch(event){
		case DRIFT_SHELL_START:{
			TracyCZoneN(ZONE_INIT, "SDL Init", true);
			SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
			int err = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER);
			DRIFT_ASSERT(err == 0, "SDL_Init() error: %s", SDL_GetError());
			
			SDL_version sdl_version;
			SDL_GetVersion(&sdl_version);
			DRIFT_LOG("Using SDL v%d.%d.%d", sdl_version.major, sdl_version.minor, sdl_version.patch);
			TracyCZoneEnd(ZONE_INIT);
			
			TracyCZoneN(ZONE_VOLK, "Volk Init", true);
			VkResult result = volkInitialize();
			AssertSuccess(result, "Failed to initialize Volk.");
			TracyCZoneEnd(ZONE_VOLK);
			
			TracyCZoneN(ZONE_WINDOW, "Open Window", true);
			if(APP->window_w == 0){
				APP->window_x = SDL_WINDOWPOS_CENTERED;
				APP->window_y = SDL_WINDOWPOS_CENTERED;
				APP->window_w = DRIFT_APP_DEFAULT_SCREEN_W;
				APP->window_h = DRIFT_APP_DEFAULT_SCREEN_H;
			}
			
			u32 window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN;
			if(APP->fullscreen) window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
			
			APP->shell_window = SDL_CreateWindow("Veridian Expanse", APP->window_x, APP->window_y, APP->window_w, APP->window_h, window_flags);
			DRIFT_ASSERT_HARD(APP->shell_window, "Failed to create SDL Vulkan window.");
			SDL_SetWindowMinimumSize(APP->shell_window, 640, 360);
			TracyCZoneEnd(ZONE_WINDOW);
			
			SDL_PumpEvents();
			SDL_SetWindowPosition(APP->shell_window, APP->window_x, APP->window_y);
			
			{
				u8 mem_buf[64*1024];
				DriftMem* mem = DriftLinearMemMake(mem_buf, sizeof(mem_buf), "cursor memory");
				DriftImage img = DriftAssetLoadImage(mem, "gfx/cursor.qoi");
				
				SDL_Surface* cursor_surface = SDL_CreateRGBSurfaceWithFormatFrom(img.pixels, img.w, img.h, 32, img.w*4, SDL_PIXELFORMAT_RGBA32);
				DRIFT_ASSERT(cursor_surface, "Failed to create surface for cursor: %s", SDL_GetError());
				SDL_Cursor* cursor = SDL_CreateColorCursor(cursor_surface, 1, 1);
				SDL_FreeSurface(cursor_surface);
				
				DRIFT_ASSERT(cursor, "Failed to create cursor: %s", SDL_GetError());
				SDL_SetCursor(cursor);
			}
			
			TracyCZoneN(ZONE_CONTEXT, "Context", true);
			APP->shell_context = ctx = DriftVkCreateContext();
			TracyCZoneEnd(ZONE_CONTEXT);
			TracyCZoneN(ZONE_SWAP, "Swap Chain", true);
			VkExtent2D extent = DriftVkCreateSwapChain(ctx, drawable_size());
			TracyCZoneEnd(ZONE_SWAP);
			
			DriftVkSwapChain* swap = &ctx->swap_chain;
			DRIFT_LOG("Swapchain created, count: %d, format: %d, colorspace: %d, extent: (%d, %d), present mode: %d",
				swap->image_count, swap->format.format, swap->format.colorSpace,
				extent.width, extent.height, swap->present_mode
			);
			
			APP->gfx_driver = DRIFT_COPY(DriftSystemMem, ((DriftGfxDriver){
				.ctx = ctx,
				.load_shader = DriftVkShaderLoad,
				.new_pipeline = DriftVkPipelineNew,
				.new_sampler = DriftVkSamplerNew,
				.new_texture = DriftVkTextureNew,
				.new_target = DriftVkRenderTargetNew,
				.load_texture_layer = DriftVkLoadTextureLayer,
				.free_objects = DriftVkFreeObjects,
				.free_all = DriftVkFreeAll,
			}));
		} break;
		
		case DRIFT_SHELL_SHOW_WINDOW: SDL_ShowWindow(APP->shell_window); break;
		
		case DRIFT_SHELL_STOP:{
			VkDevice device = ctx->device;
			
			// Free the user loaded objects.
			DriftVkFreeAll(APP->gfx_driver);
			DriftMapDestroy(&ctx->destructors);
			
			vkDestroyCommandPool(device, ctx->command_pool, NULL);
			vkDestroyBuffer(ctx->device, ctx->staging.buffer.buffer, NULL);
			vkFreeMemory(ctx->device, ctx->staging.buffer.memory, NULL);
			for(uint i = 0; i < DRIFT_VK_STAGING_JOB_COUNT; i++){
				vkDestroyFence(device, ctx->staging.jobs[i].fence, NULL);
			}
			
			for(uint i = 0; i < DRIFT_VK_RENDERER_COUNT; i++){
				DriftVkRenderer* renderer = ctx->renderers[i];
				vkDestroyBuffer(device, renderer->buffer.buffer, NULL);
				vkFreeMemory(device, renderer->buffer.memory, NULL);
				vkDestroyDescriptorPool(device, renderer->descriptor_pool, NULL);
				vkDestroyFence(device, renderer->command_fence, NULL);
			}
			
			DriftVkDestroySwapChain(ctx);
			
			vkDestroyDevice(device, NULL);
			vkDestroySurfaceKHR(ctx->instance, ctx->surface, NULL);
			
#if DRIFT_DEBUG
			PFN_vkDestroyDebugUtilsMessengerEXT destroy_messenger = (void*)vkGetInstanceProcAddr(ctx->instance, "vkDestroyDebugUtilsMessengerEXT");
			DRIFT_ASSERT(destroy_messenger, "vkCreateDebugUtilsMessengerEXT() not found.");
			destroy_messenger(ctx->instance, ctx->messenger, NULL);
#endif
			
			vkDestroyInstance(ctx->instance, NULL);
			for(uint i = 0; i < DRIFT_VK_RENDERER_COUNT; i++) DriftDealloc(DriftSystemMem, ctx->renderers[i], sizeof(DriftVkRenderer));
			DriftDealloc(DriftSystemMem, ctx, sizeof(*ctx));
			DriftDealloc(DriftSystemMem, (void*)APP->gfx_driver, sizeof(APP->gfx_driver));
			
			DRIFT_LOG("SDL Shutdown.");
			SDL_Quit();
		} break;
		
		case DRIFT_SHELL_BEGIN_FRAME:{
			DriftVkRenderer* renderer = ctx->renderers[ctx->renderer_index++ & (DRIFT_VK_RENDERER_COUNT - 1)];
			
			TracyCZoneN(ZONE_FENCE, "VkFence", true);
			VkResult result = vkWaitForFences(ctx->device, 1, &renderer->command_fence, true, UINT64_MAX);
			AssertSuccess(result, "Failed to wait for Vulkan fence.");
			result = vkResetFences(ctx->device, 1, &renderer->command_fence);
			AssertSuccess(result, "Failed to reset Vulkan fence.");
			TracyCZoneEnd(ZONE_FENCE);
			
			SDL_GetWindowPosition(APP->shell_window, &APP->window_x, &APP->window_y);
			SDL_GetWindowSize(APP->shell_window, &APP->window_w, &APP->window_h);

			DriftGfxRendererPrepare(&renderer->base, drawable_size(), shell_value);
			return renderer;
		} break;
		
		case DRIFT_SHELL_PRESENT_FRAME:{
			DriftVkRenderer* renderer = shell_value;
			
			TracyCZoneN(ZONE_AQUIRE, "VkAquire", true);
			VkExtent2D fb_extent = DriftVkAquireImage(APP->shell_context, drawable_size());
			renderer->base.default_extent = (DriftVec2){fb_extent.width, fb_extent.height};
			TracyCZoneEnd(ZONE_AQUIRE);
			
			TracyCZoneN(ZONE_EXECUTE, "VkExecute", true);
			DriftVkRendererExecute(renderer);
			TracyCZoneEnd(ZONE_EXECUTE);
			
			VkResult result = vkQueuePresentKHR(ctx->present_queue, &(VkPresentInfoKHR){
				.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
				.pWaitSemaphores = ctx->swap_chain.semaphores + ctx->swap_chain.image_index, .waitSemaphoreCount = 1,
				.pSwapchains = &ctx->swap_chain.swap_chain, .pImageIndices = &ctx->swap_chain.image_index, .swapchainCount = 1,
			});
			if(result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR){
				DriftVKRecreateSwapChain(ctx, drawable_size());
			} else {
				AssertSuccess(result, "Failed to present Vulkan swapchain image.");
			}
		} break;
		
		case DRIFT_SHELL_TOGGLE_FULLSCREEN:{
			SDL_SetWindowFullscreen(APP->shell_window, APP->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
		} break;
	}
	
	return NULL;
}

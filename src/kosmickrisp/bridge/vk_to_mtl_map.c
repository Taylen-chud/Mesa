/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "vk_to_mtl_map.h"

#include "kosmickrisp/bridge/mtl_types.h"

#include "util/format/u_format.h"

#include "vulkan/vulkan.h"
#include "vk_meta.h"

struct mtl_origin
vk_offset_3d_to_mtl_origin(const struct VkOffset3D *offset)
{
   struct mtl_origin ret = {
      .x = offset->x,
      .y = offset->y,
      .z = offset->z,
   };
   return ret;
}

struct mtl_size
vk_extent_3d_to_mtl_size(const struct VkExtent3D *extent)
{
   struct mtl_size ret = {
      .x = extent->width,
      .y = extent->height,
      .z = extent->depth,
   };
   return ret;
}

enum mtl_primitive_type
vk_primitive_topology_to_mtl_primitive_type(enum VkPrimitiveTopology topology)
{
   switch (topology) {
   case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
      return MTL_PRIMITIVE_TYPE_POINT;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
      return MTL_PRIMITIVE_TYPE_LINE;
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
      return MTL_PRIMITIVE_TYPE_LINE_STRIP;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"
   case VK_PRIMITIVE_TOPOLOGY_META_RECT_LIST_MESA:
#pragma GCC diagnostic pop
      /* Triangle fans are emulated meaning we'll translate the index buffer to
       * triangle list or generate a index buffer if there's none */
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
      return MTL_PRIMITIVE_TYPE_TRIANGLE;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
      return MTL_PRIMITIVE_TYPE_TRIANGLE_STRIP;
   default:
      UNREACHABLE("Primitive topology not supported!");
   }
}

enum mtl_primitive_topology_class
vk_primitive_topology_to_mtl_primitive_topology_class(
   enum VkPrimitiveTopology topology)
{
   switch (topology) {
   case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
      return MTL_PRIMITIVE_TOPOLOGY_CLASS_POINT;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
   /* Adjacency draws are unrolled to lines or triangles */
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
      return MTL_PRIMITIVE_TOPOLOGY_CLASS_LINE;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"
   case VK_PRIMITIVE_TOPOLOGY_META_RECT_LIST_MESA:
#pragma GCC diagnostic pop
      return MTL_PRIMITIVE_TOPOLOGY_CLASS_TRIANGLE;
   default:
      UNREACHABLE("Primitive topology not supported!");
   }
}

enum mtl_load_action
vk_attachment_load_op_to_mtl_load_action(enum VkAttachmentLoadOp op)
{
   switch (op) {
   case VK_ATTACHMENT_LOAD_OP_LOAD:
      return MTL_LOAD_ACTION_LOAD;
   case VK_ATTACHMENT_LOAD_OP_CLEAR:
      return MTL_LOAD_ACTION_CLEAR;
   case VK_ATTACHMENT_LOAD_OP_DONT_CARE:
   case VK_ATTACHMENT_LOAD_OP_NONE:
      return MTL_LOAD_ACTION_DONT_CARE;
   default:
      assert(false && "Unsupported VkAttachmentLoadOp");
      return MTL_LOAD_ACTION_DONT_CARE;
   };
}

enum mtl_store_action
vk_attachment_store_op_to_mtl_store_action(enum VkAttachmentStoreOp op)
{
   switch (op) {
   case VK_ATTACHMENT_STORE_OP_STORE:
      return MTL_STORE_ACTION_STORE;
   case VK_ATTACHMENT_STORE_OP_DONT_CARE:
   case VK_ATTACHMENT_STORE_OP_NONE:
      return MTL_STORE_ACTION_DONT_CARE;
   default:
      assert(false && "Unsupported VkAttachmentStoreOp");
      return MTL_STORE_ACTION_UNKNOWN;
   };
}

enum mtl_sampler_address_mode
vk_sampler_address_mode_to_mtl_sampler_address_mode(
   enum VkSamplerAddressMode mode)
{
   switch (mode) {
   case VK_SAMPLER_ADDRESS_MODE_REPEAT:
      return MTL_SAMPLER_ADDRESS_MODE_REPEAT;
   case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
      return MTL_SAMPLER_ADDRESS_MODE_MIRROR_REPEAT;
   case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
      return MTL_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
   case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
      return MTL_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER_COLOR;
   case VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:
      return MTL_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
   default:
      UNREACHABLE("Unsupported address mode");
   }
}

enum mtl_sampler_border_color
vk_border_color_to_mtl_sampler_border_color(enum VkBorderColor color)
{
   switch (color) {
   case VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
   case VK_BORDER_COLOR_INT_TRANSPARENT_BLACK:
      return MTL_SAMPLER_BORDER_COLOR_TRANSPARENT_BLACK;
   case VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
   case VK_BORDER_COLOR_INT_OPAQUE_BLACK:
      return MTL_SAMPLER_BORDER_COLOR_OPAQUE_BLACK;
   case VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
   case VK_BORDER_COLOR_INT_OPAQUE_WHITE:
      return MTL_SAMPLER_BORDER_COLOR_OPAQUE_WHITE;
   default:
      UNREACHABLE("Unsupported address mode");
   }
}

enum mtl_sampler_min_mag_filter
vk_filter_to_mtl_sampler_min_mag_filter(enum VkFilter filter)
{
   switch (filter) {
   case VK_FILTER_NEAREST:
      return MTL_SAMPLER_MIN_MAG_FILTER_NEAREST;
   case VK_FILTER_LINEAR:
      return MTL_SAMPLER_MIN_MAG_FILTER_LINEAR;
   default:
      UNREACHABLE("Unsupported address mode");
   }
}

enum mtl_sampler_mip_filter
vk_sampler_mipmap_mode_to_mtl_sampler_mip_filter(enum VkSamplerMipmapMode mode)
{
   switch (mode) {
   case VK_SAMPLER_MIPMAP_MODE_NEAREST:
      return MTL_SAMPLER_MIP_FILTER_NEAREST;
   case VK_SAMPLER_MIPMAP_MODE_LINEAR:
      return MTL_SAMPLER_MIP_FILTER_LINEAR;
   default:
      UNREACHABLE("Unsupported address mode");
   }
}

enum mtl_sampler_reduction_mode
vk_sampler_reduction_mode_to_mtl_sampler_reduction_mode(
   enum VkSamplerReductionMode mode)
{
   switch (mode) {
   case VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE:
      return MTL_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE;
   case VK_SAMPLER_REDUCTION_MODE_MIN:
      return MTL_SAMPLER_REDUCTION_MODE_MINIMUM;
   case VK_SAMPLER_REDUCTION_MODE_MAX:
      return MTL_SAMPLER_REDUCTION_MODE_MAXIMUM;
   default:
      UNREACHABLE("Unsupported sampler reduction mode");
   }
}

enum mtl_compare_function
vk_compare_op_to_mtl_compare_function(enum VkCompareOp op)
{
   switch (op) {
   case VK_COMPARE_OP_NEVER:
      return MTL_COMPARE_FUNCTION_NEVER;
   case VK_COMPARE_OP_LESS:
      return MTL_COMPARE_FUNCTION_LESS;
   case VK_COMPARE_OP_EQUAL:
      return MTL_COMPARE_FUNCTION_EQUAL;
   case VK_COMPARE_OP_LESS_OR_EQUAL:
      return MTL_COMPARE_FUNCTION_LESS_EQUAL;
   case VK_COMPARE_OP_GREATER:
      return MTL_COMPARE_FUNCTION_GREATER;
   case VK_COMPARE_OP_NOT_EQUAL:
      return MTL_COMPARE_FUNCTION_NOT_EQUAL;
   case VK_COMPARE_OP_GREATER_OR_EQUAL:
      return MTL_COMPARE_FUNCTION_GREATER_EQUAL;
   case VK_COMPARE_OP_ALWAYS:
      return MTL_COMPARE_FUNCTION_ALWAYS;
   default:
      UNREACHABLE("Unsupported address mode");
   }
}

enum mtl_winding
vk_front_face_to_mtl_winding(enum VkFrontFace face)
{
   switch (face) {
   case VK_FRONT_FACE_CLOCKWISE:
      return MTL_WINDING_CLOCKWISE;
   case VK_FRONT_FACE_COUNTER_CLOCKWISE:
      return MTL_WINDING_COUNTER_CLOCKWISE;
   default:
      assert(false && "Unsupported VkFrontFace");
      return MTL_WINDING_CLOCKWISE;
   }
}

enum mtl_cull_mode
vk_front_face_to_mtl_cull_mode(enum VkCullModeFlagBits mode)
{
   switch (mode) {
   case VK_CULL_MODE_NONE:
   case VK_CULL_MODE_FRONT_AND_BACK: // Emulated with scissor
      return MTL_CULL_MODE_NONE;
   case VK_CULL_MODE_FRONT_BIT:
      return MTL_CULL_MODE_FRONT;
   case VK_CULL_MODE_BACK_BIT:
      return MTL_CULL_MODE_BACK;
   default:
      UNREACHABLE("Unsupported VkCullModeFlags");
   }
}

enum mtl_depth_resolve_filter
vk_resolve_mode_to_mtl_depth_resolve_filter(
   enum VkResolveModeFlagBits resolve_mode)
{
   switch (resolve_mode) {
   case VK_RESOLVE_MODE_SAMPLE_ZERO_BIT:
      return MTL_DEPTH_RESOLVE_FILTER_SAMPLE_0;
   case VK_RESOLVE_MODE_MIN_BIT:
      return MTL_DEPTH_RESOLVE_FILTER_MIN;
   case VK_RESOLVE_MODE_MAX_BIT:
      return MTL_DEPTH_RESOLVE_FILTER_MAX;
   default:
      UNREACHABLE("Unsupported VkResolveMode for depth");
   }
}

enum mtl_stencil_resolve_filter
vk_resolve_mode_to_mtl_stencil_resolve_filter(
   enum VkResolveModeFlagBits resolve_mode)
{
   switch (resolve_mode) {
   case VK_RESOLVE_MODE_SAMPLE_ZERO_BIT:
      return MTL_STENCIL_RESOLVE_FILTER_SAMPLE_0;
   default:
      UNREACHABLE("Unsupported VkResolveMode for stencil");
   }
}

enum mtl_index_type
index_size_in_bytes_to_mtl_index_type(unsigned bytes)
{
   switch (bytes) {
   case 2u:
      return MTL_INDEX_TYPE_UINT16;
   case 4u:
      return MTL_INDEX_TYPE_UINT32;
   default:
      UNREACHABLE("Unsupported byte size for index");
   }
}

enum mtl_index_type
vk_index_type_to_mtl_index_type(enum VkIndexType index_type)
{
   switch (index_type) {
   case VK_INDEX_TYPE_UINT16:
      return MTL_INDEX_TYPE_UINT16;
   case VK_INDEX_TYPE_UINT32:
      return MTL_INDEX_TYPE_UINT32;
   case VK_INDEX_TYPE_NONE_KHR:
      return MTL_INDEX_TYPE_UINT16; /* Won't be used, but need a valid value */
   default:
      UNREACHABLE("Unsupported VkIndexType");
   }
}

unsigned
mtl_index_type_to_size_B(enum mtl_index_type type)
{
   switch (type) {
   case MTL_INDEX_TYPE_UINT16:
      return 2u;
   case MTL_INDEX_TYPE_UINT32:
      return 4u;
   default:
      UNREACHABLE("Unhandled index type");
   }
}

const char *
mtl_command_queue_error_to_string(enum mtl_command_queue_error error)
{
   switch (error) {
   case MTL_COMMAND_QUEUE_ERROR_NONE:
      return "MTL_COMMAND_QUEUE_ERROR_NONE";
   case MTL_COMMAND_QUEUE_ERROR_TIMEOUT:
      return "MTL_COMMAND_QUEUE_ERROR_TIMEOUT";
   case MTL_COMMAND_QUEUE_ERROR_NOT_PERMITTED:
      return "MTL_COMMAND_QUEUE_ERROR_NOT_PERMITTED";
   case MTL_COMMAND_QUEUE_ERROR_OUT_OF_MEMORY:
      return "MTL_COMMAND_QUEUE_ERROR_OUT_OF_MEMORY";
   case MTL_COMMAND_QUEUE_ERROR_ACCESS_REVOKED:
      return "MTL_COMMAND_QUEUE_ERROR_ACCESS_REVOKED";
   case MTL_COMMAND_QUEUE_ERROR_INTERNAL:
      return "MTL_COMMAND_QUEUE_ERROR_INTERNAL";
   default:
      UNREACHABLE("Unsupported error value");
   }
}

enum mtl_pixel_format
vk_format_to_mtl_pixel_format(VkFormat format)
{
   switch (format) {
   /* 8-bit */
   case VK_FORMAT_R8_UNORM:        return MTL_PIXEL_FORMAT_R8_UNORM;
   case VK_FORMAT_R8_SNORM:        return MTL_PIXEL_FORMAT_R8_SNORM;
   case VK_FORMAT_R8_UINT:         return MTL_PIXEL_FORMAT_R8_UINT;
   case VK_FORMAT_R8_SINT:         return MTL_PIXEL_FORMAT_R8_SINT;
   case VK_FORMAT_R8_SRGB:         return MTL_PIXEL_FORMAT_R8_SRGB;

   /* 16-bit */
   case VK_FORMAT_R16_UNORM:       return MTL_PIXEL_FORMAT_R16_UNORM;
   case VK_FORMAT_R16_SNORM:       return MTL_PIXEL_FORMAT_R16_SNORM;
   case VK_FORMAT_R16_UINT:        return MTL_PIXEL_FORMAT_R16_UINT;
   case VK_FORMAT_R16_SINT:        return MTL_PIXEL_FORMAT_R16_SINT;
   case VK_FORMAT_R16_SFLOAT:      return MTL_PIXEL_FORMAT_R16_FLOAT;

   case VK_FORMAT_R8G8_UNORM:      return MTL_PIXEL_FORMAT_R8G8_UNORM;
   case VK_FORMAT_R8G8_SNORM:      return MTL_PIXEL_FORMAT_R8G8_SNORM;
   case VK_FORMAT_R8G8_UINT:       return MTL_PIXEL_FORMAT_R8G8_UINT;
   case VK_FORMAT_R8G8_SINT:       return MTL_PIXEL_FORMAT_R8G8_SINT;
   case VK_FORMAT_R8G8_SRGB:       return MTL_PIXEL_FORMAT_R8G8_SRGB;

   /* Packed 16-bit */
   case VK_FORMAT_B5G6R5_UNORM_PACK16:  return MTL_PIXEL_FORMAT_B5G6R5_UNORM;
   case VK_FORMAT_A1R5G5B5_UNORM_PACK16: return MTL_PIXEL_FORMAT_A1B5G5R5_UNORM;
   case VK_FORMAT_A4R4G4B4_UNORM_PACK16: return MTL_PIXEL_FORMAT_A4B4G4R4_UNORM;
   case VK_FORMAT_R5G5B5A1_UNORM_PACK16: return MTL_PIXEL_FORMAT_B5G5R5A1_UNORM;

   /* 32-bit */
   case VK_FORMAT_R32_UINT:        return MTL_PIXEL_FORMAT_R32_UINT;
   case VK_FORMAT_R32_SINT:        return MTL_PIXEL_FORMAT_R32_SINT;
   case VK_FORMAT_R32_SFLOAT:      return MTL_PIXEL_FORMAT_R32_FLOAT;

   case VK_FORMAT_R16G16_UNORM:    return MTL_PIXEL_FORMAT_R16G16_UNORM;
   case VK_FORMAT_R16G16_SNORM:    return MTL_PIXEL_FORMAT_R16G16_SNORM;
   case VK_FORMAT_R16G16_UINT:     return MTL_PIXEL_FORMAT_R16G16_UINT;
   case VK_FORMAT_R16G16_SINT:     return MTL_PIXEL_FORMAT_R16G16_SINT;
   case VK_FORMAT_R16G16_SFLOAT:   return MTL_PIXEL_FORMAT_R16G16_FLOAT;

   case VK_FORMAT_R8G8B8A8_UNORM:  return MTL_PIXEL_FORMAT_R8G8B8A8_UNORM;
   case VK_FORMAT_R8G8B8A8_SNORM:  return MTL_PIXEL_FORMAT_R8G8B8A8_SNORM;
   case VK_FORMAT_R8G8B8A8_UINT:   return MTL_PIXEL_FORMAT_R8G8B8A8_UINT;
   case VK_FORMAT_R8G8B8A8_SINT:   return MTL_PIXEL_FORMAT_R8G8B8A8_SINT;
   case VK_FORMAT_R8G8B8A8_SRGB:   return MTL_PIXEL_FORMAT_R8G8B8A8_SRGB;

   case VK_FORMAT_B8G8R8A8_UNORM:  return MTL_PIXEL_FORMAT_B8G8R8A8_UNORM;
   case VK_FORMAT_B8G8R8A8_SRGB:   return MTL_PIXEL_FORMAT_B8G8R8A8_SRGB;

   /* Packed 32-bit */
   case VK_FORMAT_A2R10G10B10_UNORM_PACK32: return MTL_PIXEL_FORMAT_R10G10B10A2_UNORM;
   case VK_FORMAT_A2R10G10B10_UINT_PACK32:  return MTL_PIXEL_FORMAT_R10G10B10A2_UINT;

   /* 64-bit */
   case VK_FORMAT_R32G32_UINT:     return MTL_PIXEL_FORMAT_R32G32_UINT;
   case VK_FORMAT_R32G32_SINT:     return MTL_PIXEL_FORMAT_R32G32_SINT;
   case VK_FORMAT_R32G32_SFLOAT:   return MTL_PIXEL_FORMAT_R32G32_FLOAT;

   case VK_FORMAT_R16G16B16A16_UNORM: return MTL_PIXEL_FORMAT_R16G16B16A16_UNORM;
   case VK_FORMAT_R16G16B16A16_SNORM: return MTL_PIXEL_FORMAT_R16G16B16A16_SNORM;
   case VK_FORMAT_R16G16B16A16_UINT:  return MTL_PIXEL_FORMAT_R16G16B16A16_UINT;
   case VK_FORMAT_R16G16B16A16_SINT:  return MTL_PIXEL_FORMAT_R16G16B16A16_SINT;
   case VK_FORMAT_R16G16B16A16_SFLOAT: return MTL_PIXEL_FORMAT_R16G16B16A16_FLOAT;

   /* 128-bit */
   case VK_FORMAT_R32G32B32A32_UINT:  return MTL_PIXEL_FORMAT_R32G32B32A32_UINT;
   case VK_FORMAT_R32G32B32A32_SINT:  return MTL_PIXEL_FORMAT_R32G32B32A32_SINT;
   case VK_FORMAT_R32G32B32A32_SFLOAT: return MTL_PIXEL_FORMAT_R32G32B32A32_FLOAT;

   /* Depth */
   case VK_FORMAT_D16_UNORM:       return MTL_PIXEL_FORMAT_Z16_UNORM;
   case VK_FORMAT_D32_SFLOAT:      return MTL_PIXEL_FORMAT_Z32_FLOAT;
   case VK_FORMAT_D24_UNORM_S8_UINT: return MTL_PIXEL_FORMAT_Z24_UNORM_S8_UINT;
   case VK_FORMAT_D32_SFLOAT_S8_UINT: return MTL_PIXEL_FORMAT_Z32_FLOAT_S8X24_UINT;

   /* Stencil */
   case VK_FORMAT_S8_UINT:         return MTL_PIXEL_FORMAT_S8_UINT;

   default:
      return MTL_PIXEL_FORMAT_INVALID;
   }
}

enum mtl_vertex_format
vk_format_to_mtl_vertex_format(VkFormat format)
{
   switch (format) {
   case VK_FORMAT_R8_UNORM:        return MTL_VERTEX_FORMAT_UCHAR4_NORMALIZED;
   case VK_FORMAT_R8_SNORM:        return MTL_VERTEX_FORMAT_CHAR4_NORMALIZED;
   case VK_FORMAT_R8_UINT:         return MTL_VERTEX_FORMAT_UINT;
   case VK_FORMAT_R8_SINT:         return MTL_VERTEX_FORMAT_INT;

   case VK_FORMAT_R8G8_UNORM:      return MTL_VERTEX_FORMAT_UCHAR2_NORMALIZED;
   case VK_FORMAT_R8G8_SNORM:      return MTL_VERTEX_FORMAT_CHAR2_NORMALIZED;
   case VK_FORMAT_R8G8_UINT:       return MTL_VERTEX_FORMAT_UINT2;
   case VK_FORMAT_R8G8_SINT:       return MTL_VERTEX_FORMAT_INT2;

   case VK_FORMAT_R8G8B8A8_UNORM:  return MTL_VERTEX_FORMAT_UCHAR4_NORMALIZED;
   case VK_FORMAT_R8G8B8A8_SNORM:  return MTL_VERTEX_FORMAT_CHAR4_NORMALIZED;
   case VK_FORMAT_R8G8B8A8_UINT:   return MTL_VERTEX_FORMAT_UINT4;
   case VK_FORMAT_R8G8B8A8_SINT:   return MTL_VERTEX_FORMAT_INT4;

   case VK_FORMAT_R16_UNORM:       return MTL_VERTEX_FORMAT_USHORT2_NORMALIZED;
   case VK_FORMAT_R16_SNORM:       return MTL_VERTEX_FORMAT_SHORT2_NORMALIZED;
   case VK_FORMAT_R16_UINT:        return MTL_VERTEX_FORMAT_UINT;
   case VK_FORMAT_R16_SINT:        return MTL_VERTEX_FORMAT_INT;
   case VK_FORMAT_R16_SFLOAT:      return MTL_VERTEX_FORMAT_HALF2;

   case VK_FORMAT_R16G16_UNORM:    return MTL_VERTEX_FORMAT_USHORT2_NORMALIZED;
   case VK_FORMAT_R16G16_SNORM:    return MTL_VERTEX_FORMAT_SHORT2_NORMALIZED;
   case VK_FORMAT_R16G16_UINT:     return MTL_VERTEX_FORMAT_UINT2;
   case VK_FORMAT_R16G16_SINT:     return MTL_VERTEX_FORMAT_INT2;
   case VK_FORMAT_R16G16_SFLOAT:   return MTL_VERTEX_FORMAT_HALF2;

   case VK_FORMAT_R16G16B16A16_UNORM: return MTL_VERTEX_FORMAT_USHORT4_NORMALIZED;
   case VK_FORMAT_R16G16B16A16_SNORM: return MTL_VERTEX_FORMAT_SHORT4_NORMALIZED;
   case VK_FORMAT_R16G16B16A16_UINT:  return MTL_VERTEX_FORMAT_UINT4;
   case VK_FORMAT_R16G16B16A16_SINT:  return MTL_VERTEX_FORMAT_INT4;
   case VK_FORMAT_R16G16B16A16_SFLOAT: return MTL_VERTEX_FORMAT_HALF4;

   case VK_FORMAT_R32_UINT:        return MTL_VERTEX_FORMAT_UINT;
   case VK_FORMAT_R32_SINT:        return MTL_VERTEX_FORMAT_INT;
   case VK_FORMAT_R32_SFLOAT:      return MTL_VERTEX_FORMAT_FLOAT;

   case VK_FORMAT_R32G32_UINT:     return MTL_VERTEX_FORMAT_UINT2;
   case VK_FORMAT_R32G32_SINT:     return MTL_VERTEX_FORMAT_INT2;
   case VK_FORMAT_R32G32_SFLOAT:   return MTL_VERTEX_FORMAT_FLOAT2;

   case VK_FORMAT_R32G32B32_UINT:  return MTL_VERTEX_FORMAT_UINT3;
   case VK_FORMAT_R32G32B32_SINT:  return MTL_VERTEX_FORMAT_INT3;
   case VK_FORMAT_R32G32B32_SFLOAT: return MTL_VERTEX_FORMAT_FLOAT3;

   case VK_FORMAT_R32G32B32A32_UINT: return MTL_VERTEX_FORMAT_UINT4;
   case VK_FORMAT_R32G32B32A32_SINT: return MTL_VERTEX_FORMAT_INT4;
   case VK_FORMAT_R32G32B32A32_SFLOAT: return MTL_VERTEX_FORMAT_FLOAT4;

   case VK_FORMAT_B10G11R11_UFLOAT_PACK32: return MTL_VERTEX_FORMAT_INVALID;
   case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:  return MTL_VERTEX_FORMAT_INVALID;
   case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return MTL_VERTEX_FORMAT_UINT1010102_NORMALIZED;
   case VK_FORMAT_A2B10G10R10_UINT_PACK32:  return MTL_VERTEX_FORMAT_UINT1010102_NORMALIZED;

   default:
      return MTL_VERTEX_FORMAT_INVALID;
   }
}

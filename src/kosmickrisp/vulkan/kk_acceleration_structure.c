/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_acceleration_structure.h"

#include "kk_bo.h"
#include "kk_buffer.h"
#include "kk_cmd_buffer.h"
#include "kk_device.h"
#include "kk_device_memory.h"
#include "kk_entrypoints.h"
#include "kk_physical_device.h"

#include "kosmickrisp/bridge/mtl_bridge.h"
#include "kosmickrisp/bridge/mtl_device.h"
#include "kosmickrisp/bridge/mtl_types.h"
#include "kosmickrisp/bridge/vk_to_mtl_map.h"

#include "util/hash_table.h"
#include "util/ralloc.h"
#include "util/u_math.h"

#include "vk_acceleration_structure.h"
#include "vk_buffer.h"
#include "vk_device.h"
#include "vk_log.h"
#include "vk_format.h"
#include "vk_object.h"
#include "util/macros.h"

/* Metal acceleration structure type mapping */
static enum mtl_acceleration_structure_type
vk_as_type_to_mtl(VkAccelerationStructureTypeKHR type)
{
   switch (type) {
   case VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR:
      return MTL_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
   case VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR:
      return MTL_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
   case VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR:
      return MTL_ACCELERATION_STRUCTURE_TYPE_GENERIC;
   default:
      UNREACHABLE("Invalid acceleration structure type");
   }
}

/* Metal acceleration structure build mode mapping */
static enum mtl_acceleration_structure_build_mode
vk_build_mode_to_mtl(VkBuildAccelerationStructureModeKHR mode)
{
   switch (mode) {
   case VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR:
      return MTL_ACCELERATION_STRUCTURE_BUILD_MODE_BUILD;
   case VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR:
      return MTL_ACCELERATION_STRUCTURE_BUILD_MODE_UPDATE;
   default:
      UNREACHABLE("Invalid build mode");
   }
}

/* Metal acceleration structure build flags mapping */
static enum mtl_acceleration_structure_build_flags
vk_build_flags_to_mtl(VkBuildAccelerationStructureFlagsKHR flags)
{
   enum mtl_acceleration_structure_build_flags mtl_flags = 0;
   if (flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR)
      mtl_flags |= MTL_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
   if (flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR)
      mtl_flags |= MTL_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
   if (flags & VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR)
      mtl_flags |= MTL_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
   if (flags & VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR)
      mtl_flags |= MTL_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
   if (flags & VK_BUILD_ACCELERATION_STRUCTURE_LOW_MEMORY_BIT_KHR)
      mtl_flags |= MTL_ACCELERATION_STRUCTURE_BUILD_FLAG_LOW_MEMORY;
   return mtl_flags;
}

/* Helper functions for geometry type and flag translation */
static enum mtl_acceleration_structure_geometry_type
vk_as_geometry_type_to_mtl(VkGeometryTypeKHR type)
{
   switch (type) {
   case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
      return MTL_ACCELERATION_STRUCTURE_GEOMETRY_TYPE_TRIANGLES;
   case VK_GEOMETRY_TYPE_AABBS_KHR:
      return MTL_ACCELERATION_STRUCTURE_GEOMETRY_TYPE_AABBS;
   case VK_GEOMETRY_TYPE_INSTANCES_KHR:
      return MTL_ACCELERATION_STRUCTURE_GEOMETRY_TYPE_INSTANCES;
   default:
      return MTL_ACCELERATION_STRUCTURE_GEOMETRY_TYPE_TRIANGLES;
   }
}

static enum mtl_acceleration_structure_geometry_flags
vk_geometry_flags_to_mtl(VkGeometryFlagsKHR flags)
{
   enum mtl_acceleration_structure_geometry_flags mtl_flags = 0;
   if (flags & VK_GEOMETRY_OPAQUE_BIT_KHR)
      mtl_flags |= MTL_ACCELERATION_STRUCTURE_GEOMETRY_FLAG_OPAQUE;
   if (flags & VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR)
      mtl_flags |= MTL_ACCELERATION_STRUCTURE_GEOMETRY_FLAG_NO_DUPLICATE_ANY_HIT_INVOCATION;
   return mtl_flags;
}

static uint64_t
vk_device_or_host_address_to_uint64(const VkDeviceOrHostAddressConstKHR addr)
{
   return addr.deviceAddress;
}

VkResult
kk_acceleration_structure_create(struct kk_device *dev,
                                 const VkAccelerationStructureCreateInfoKHR *pCreateInfo,
                                 const VkAllocationCallbacks *pAllocator,
                                 struct kk_acceleration_structure **out)
{
   struct kk_physical_device *pdev = kk_device_physical(dev);

   struct kk_acceleration_structure *as =
      vk_object_zalloc(&dev->vk, pAllocator, sizeof(*as),
                       VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR);
   if (!as)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   enum mtl_acceleration_structure_type mtl_type =
      vk_as_type_to_mtl(pCreateInfo->type);

   /* Create Metal acceleration structure */
   as->mtl_as = mtl_acceleration_structure_create(dev->mtl_handle,
                                                   mtl_type,
                                                   pCreateInfo->size);
   if (!as->mtl_as) {
      vk_object_free(&dev->vk, pAllocator, as);
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   }

   as->size = pCreateInfo->size;
   as->is_built = false;

   *out = as;
   return VK_SUCCESS;
}

void
kk_acceleration_structure_destroy(struct kk_device *dev,
                                  struct kk_acceleration_structure *as,
                                  const VkAllocationCallbacks *pAllocator)
{
   if (!as)
      return;

   mtl_release(as->mtl_as);
   vk_object_free(&dev->vk, pAllocator, as);
}

static VkResult
translate_geometry(struct kk_device *dev,
                   const VkAccelerationStructureGeometryKHR *vk_geom,
                   struct mtl_acceleration_structure_geometry *mtl_geom)
{
   memset(mtl_geom, 0, sizeof(*mtl_geom));

   mtl_geom->type = vk_as_geometry_type_to_mtl(vk_geom->geometryType);
   mtl_geom->flags = vk_geometry_flags_to_mtl(vk_geom->flags);

   switch (vk_geom->geometryType) {
   case VK_GEOMETRY_TYPE_TRIANGLES_KHR: {
      const VkAccelerationStructureGeometryTrianglesDataKHR *tri = &vk_geom->geometry.triangles;

      mtl_geom->triangles.vertex_format = vk_format_to_mtl_pixel_format(tri->vertexFormat);
      mtl_geom->triangles.vertex_stride = tri->vertexStride;
      mtl_geom->triangles.max_vertex = tri->maxVertex;
      mtl_geom->triangles.index_type = vk_index_type_to_mtl_index_type(tri->indexType);
      mtl_geom->triangles.transform_data = tri->transformData;

      /* Vertex and index data are VkDeviceAddress (GPU addresses) */
      mtl_geom->triangles.vertex_buffer = vk_device_or_host_address_to_uint64(tri->vertexData);
      mtl_geom->triangles.index_buffer = vk_device_or_host_address_to_uint64(tri->indexData);
      break;
   }
   case VK_GEOMETRY_TYPE_AABBS_KHR: {
      const VkAccelerationStructureGeometryAabbsDataKHR *aabbs = &vk_geom->geometry.aabbs;

      mtl_geom->aabbs.stride = aabbs->stride;
      mtl_geom->aabbs.aabb_buffer = vk_device_or_host_address_to_uint64(aabbs->data);
      break;
   }
   case VK_GEOMETRY_TYPE_INSTANCES_KHR: {
      const VkAccelerationStructureGeometryInstancesDataKHR *instances = &vk_geom->geometry.instances;
      mtl_geom->instances.instance_buffer = vk_device_or_host_address_to_uint64(instances->data);
      mtl_geom->instances.instance_count = instances->arrayOfPointers ? 0 : 0; /* TODO: handle arrayOfPointers */
      break;
   }
   default:
      return VK_ERROR_UNKNOWN;
   }

   return VK_SUCCESS;
}

VkResult
kk_acceleration_structure_build(struct kk_cmd_buffer *cmd,
                                struct kk_acceleration_structure *dst,
                                const struct kk_acceleration_structure_build_info *info)
{
   struct kk_device *dev = kk_cmd_buffer_device(cmd);
   struct mtl_acceleration_structure_build_info mtl_info = {0};

   mtl_info.type = vk_as_type_to_mtl(info->type);
   mtl_info.mode = vk_build_mode_to_mtl(info->mode);
   mtl_info.flags = vk_build_flags_to_mtl(info->flags);
   mtl_info.dst = dst->mtl_as;
   mtl_info.src = info->src ? ((struct kk_acceleration_structure *)info->src)->mtl_as : NULL;
   mtl_info.geometry_count = info->geometry_count;

   struct mtl_acceleration_structure_geometry *mtl_geoms =
      ralloc_array(cmd, struct mtl_acceleration_structure_geometry,
                   info->geometry_count);
   if (!mtl_geoms)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   for (uint32_t i = 0; i < info->geometry_count; i++) {
      VkResult result = translate_geometry(dev, &info->geometries[i], &mtl_geoms[i]);
      if (result != VK_SUCCESS) {
         ralloc_free(mtl_geoms);
         return result;
      }
   }

   mtl_info.geometries = mtl_geoms;
   mtl_info.ranges = info->ranges;

   mtl_acceleration_structure_command_encoder *as_encoder =
      mtl_new_acceleration_structure_command_encoder(cmd->metal.cmd_buf);
   if (!as_encoder) {
      ralloc_free(mtl_geoms);
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   }

   mtl_acceleration_structure_encoder_build(as_encoder, &mtl_info);
   mtl_end_encoding(as_encoder);

   dst->is_built = true;
   ralloc_free(mtl_geoms);

   return VK_SUCCESS;
}

VkResult
kk_acceleration_structure_copy(struct kk_cmd_buffer *cmd,
                               struct kk_acceleration_structure *dst,
                               struct kk_acceleration_structure *src,
                               VkCopyAccelerationStructureModeKHR mode)
{
   struct kk_device *dev = kk_cmd_buffer_device(cmd);

   mtl_acceleration_structure_command_encoder *as_encoder =
      mtl_new_acceleration_structure_command_encoder(cmd->metal.cmd_buf);
   if (!as_encoder)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   enum mtl_copy_acceleration_structure_mode mtl_mode;
   switch (mode) {
   case VK_COPY_ACCELERATION_STRUCTURE_MODE_CLONE_KHR:
      mtl_mode = MTL_COPY_ACCELERATION_STRUCTURE_MODE_CLONE;
      break;
   case VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR:
      mtl_mode = MTL_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT;
      break;
   case VK_COPY_ACCELERATION_STRUCTURE_MODE_SERIALIZE_KHR:
      mtl_mode = MTL_COPY_ACCELERATION_STRUCTURE_MODE_SERIALIZE;
      break;
   case VK_COPY_ACCELERATION_STRUCTURE_MODE_DESERIALIZE_KHR:
      mtl_mode = MTL_COPY_ACCELERATION_STRUCTURE_MODE_DESERIALIZE;
      break;
   default:
      mtl_end_encoding(as_encoder);
      return VK_ERROR_UNKNOWN;
   }

   mtl_acceleration_structure_encoder_copy(as_encoder, dst->mtl_as, src->mtl_as, mtl_mode);
   mtl_end_encoding(as_encoder);

   return VK_SUCCESS;
}

void
kk_cmd_build_acceleration_structure(struct kk_cmd_buffer *cmd,
                                    uint32_t info_count,
                                    const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
                                    const VkAccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos)
{
   for (uint32_t i = 0; i < info_count; i++) {
      const VkAccelerationStructureBuildGeometryInfoKHR *info = &pInfos[i];
      VK_FROM_HANDLE(kk_acceleration_structure, dst, info->dstAccelerationStructure);
      VK_FROM_HANDLE(kk_acceleration_structure, src, info->srcAccelerationStructure);

      struct kk_acceleration_structure_build_info build_info = {
         .type = info->type,
         .flags = info->flags,
         .mode = info->mode,
         .src = info->srcAccelerationStructure,
         .dst = info->dstAccelerationStructure,
         .geometry_count = info->geometryCount,
         .geometries = info->pGeometries,
         .ranges = ppBuildRangeInfos ? ppBuildRangeInfos[i] : NULL,
      };

      kk_acceleration_structure_build(cmd, dst, &build_info);
   }
}

void
kk_cmd_copy_acceleration_structure(struct kk_cmd_buffer *cmd,
                                   const VkCopyAccelerationStructureInfoKHR *pInfo)
{
   VK_FROM_HANDLE(kk_acceleration_structure, dst, pInfo->dst);
   VK_FROM_HANDLE(kk_acceleration_structure, src, pInfo->src);

   kk_acceleration_structure_copy(cmd, dst, src, pInfo->mode);
}

void
kk_cmd_copy_acceleration_structure_to_memory(struct kk_cmd_buffer *cmd,
                                             const VkCopyAccelerationStructureToMemoryInfoKHR *pInfo)
{
   VK_FROM_HANDLE(kk_acceleration_structure, src, pInfo->src);
   VK_FROM_HANDLE(kk_buffer, dst, pInfo->dst.buffer);

   mtl_acceleration_structure_command_encoder *as_encoder =
      mtl_new_acceleration_structure_command_encoder(cmd->metal.cmd_buf);
   if (!as_encoder)
      return;

   mtl_acceleration_structure_encoder_copy_to_buffer(as_encoder, src->mtl_as,
                                                      dst->metal.handle + pInfo->dst.offset);
   mtl_end_encoding(as_encoder);
}

void
kk_cmd_copy_memory_to_acceleration_structure(struct kk_cmd_buffer *cmd,
                                             const VkCopyMemoryToAccelerationStructureInfoKHR *pInfo)
{
   VK_FROM_HANDLE(kk_buffer, src, pInfo->src.buffer);
   VK_FROM_HANDLE(kk_acceleration_structure, dst, pInfo->dst);

   mtl_acceleration_structure_command_encoder *as_encoder =
      mtl_new_acceleration_structure_command_encoder(cmd->metal.cmd_buf);
   if (!as_encoder)
      return;

   mtl_acceleration_structure_encoder_copy_from_buffer(as_encoder, dst->mtl_as,
                                                        src->metal.handle + pInfo->src.offset);
   mtl_end_encoding(as_encoder);
}

VkResult
kk_acceleration_structure_get_device_address(struct kk_device *dev,
                                             struct kk_acceleration_structure *as,
                                             VkDeviceAddress *pAddress)
{
   *pAddress = mtl_acceleration_structure_get_gpu_address(as->mtl_as);
   return VK_SUCCESS;
}

void
kk_write_acceleration_structure_descriptor(struct kk_device *dev,
                                           VkWriteDescriptorSet *write)
{
   for (uint32_t i = 0; i < write->descriptorCount; i++) {
      VK_FROM_HANDLE(kk_acceleration_structure, as, write->pAccelerationStructures[i]);
      /* TODO: Write to descriptor set */
   }
}

VkDeviceSize
kk_acceleration_structure_get_build_size(struct kk_device *dev,
                                         const VkAccelerationStructureBuildGeometryInfoKHR *pBuildInfo,
                                         const uint32_t *pMaxPrimitiveCounts)
{
   /* TODO: Query Metal for build size */
   return 0;
}

VkResult
kk_acceleration_structure_get_capture_replay_data(struct kk_device *dev,
                                                  struct kk_acceleration_structure *as,
                                                  void *pData)
{
   /* TODO: Implement capture/replay */
   (void)dev;
   (void)as;
   (void)pData;
   return VK_ERROR_FEATURE_NOT_PRESENT;
}
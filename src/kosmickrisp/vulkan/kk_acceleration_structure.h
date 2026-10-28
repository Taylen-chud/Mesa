/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_ACCELERATION_STRUCTURE_H
#define KK_ACCELERATION_STRUCTURE_H 1

#include <stddef.h>
#include <stdbool.h>

#include "kk_device.h"

#include "vk_acceleration_structure.h"
#include "kosmickrisp/bridge/mtl_types.h"

struct kk_acceleration_structure {
   struct vk_acceleration_structure vk;

   mtl_acceleration_structure *mtl_as;
   size_t size;
   bool is_built;
};

VK_DEFINE_HANDLE_CASTS(kk_acceleration_structure, vk.base,
                       VkAccelerationStructureKHR,
                       VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR)

struct kk_acceleration_structure_build_info {
   VkAccelerationStructureTypeKHR type;
   VkBuildAccelerationStructureFlagsKHR flags;
   VkAccelerationStructureBuildModeKHR mode;
   VkAccelerationStructureKHR src;
   VkAccelerationStructureKHR dst;
   uint32_t geometry_count;
   const VkAccelerationStructureGeometryKHR *geometries;
   const VkAccelerationStructureBuildRangeInfoKHR *ranges;
};

VkResult
kk_acceleration_structure_create(struct kk_device *dev,
                                 const VkAccelerationStructureCreateInfoKHR *pCreateInfo,
                                 const VkAllocationCallbacks *pAllocator,
                                 struct kk_acceleration_structure **out);

void
kk_acceleration_structure_destroy(struct kk_device *dev,
                                  struct kk_acceleration_structure *as,
                                  const VkAllocationCallbacks *pAllocator);

VkResult
kk_acceleration_structure_build(struct kk_cmd_buffer *cmd,
                                struct kk_acceleration_structure *dst,
                                const struct kk_acceleration_structure_build_info *info);

VkResult
kk_acceleration_structure_copy(struct kk_cmd_buffer *cmd,
                               struct kk_acceleration_structure *dst,
                               struct kk_acceleration_structure *src,
                               VkCopyAccelerationStructureModeKHR mode);

void
kk_cmd_build_acceleration_structure(struct kk_cmd_buffer *cmd,
                                    uint32_t info_count,
                                    const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
                                    const VkAccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos);

void
kk_cmd_copy_acceleration_structure(struct kk_cmd_buffer *cmd,
                                   const VkCopyAccelerationStructureInfoKHR *pInfo);

void
kk_cmd_copy_acceleration_structure_to_memory(struct kk_cmd_buffer *cmd,
                                             const VkCopyAccelerationStructureToMemoryInfoKHR *pInfo);

void
kk_cmd_copy_memory_to_acceleration_structure(struct kk_cmd_buffer *cmd,
                                             const VkCopyMemoryToAccelerationStructureInfoKHR *pInfo);

VkResult
kk_acceleration_structure_get_device_address(struct kk_device *dev,
                                             struct kk_acceleration_structure *as,
                                             VkDeviceAddress *pAddress);

void
kk_write_acceleration_structure_descriptor(struct kk_device *dev,
                                           VkWriteDescriptorSet *write);

VkDeviceSize
kk_acceleration_structure_get_build_size(struct kk_device *dev,
                                         const VkAccelerationStructureBuildGeometryInfoKHR *pBuildInfo,
                                         const uint32_t *pMaxPrimitiveCounts);

VkResult
kk_acceleration_structure_get_capture_replay_data(struct kk_device *dev,
                                                  struct kk_acceleration_structure *as,
                                                  VkAccelerationStructureCaptureReplayDataKHR *pData);

#endif
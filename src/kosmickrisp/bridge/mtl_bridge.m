/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "mtl_bridge.h"

// kk_image_layout.h should also includes "vulkan/vulkan.h", but just to be safe
#include "vulkan/vulkan.h"
#include "kk_image_layout.h"

#include "util/macros.h"

#include <Metal/MTLCommandBuffer.h>
#include <Metal/MTLCommandQueue.h>
#include <Metal/MTLDevice.h>
#include <Metal/MTLHeap.h>
#include <Metal/MTLEvent.h>
#include <Metal/MTLAccelerationStructure.h>

#include <QuartzCore/CAMetalLayer.h>

static_assert(sizeof(MTLResourceID) == sizeof(uint64_t), "Must match, otherwise descriptors are broken");

mtl_texture *
mtl_drawable_get_texture(void *drawable_ptr)
{
   @autoreleasepool {
      id<CAMetalDrawable> drawable = (id<CAMetalDrawable>)drawable_ptr;
      return drawable.texture;
   }
}

void *
mtl_retain(void *handle)
{
   @autoreleasepool {
      NSObject *obj = (NSObject *)handle;
      return [obj retain];
   }
}

void
mtl_release(void *handle)
{
   @autoreleasepool {
      NSObject *obj = (NSObject *)handle;
      [obj release];
   }
}

mtl_acceleration_structure *
mtl_acceleration_structure_create(mtl_device *device,
                                  enum mtl_acceleration_structure_type type,
                                  size_t size)
{
   @autoreleasepool {
      id<MTLDevice> dev = (id<MTLDevice>)device;
      MTLAccelerationStructureDescriptor *desc = [MTLAccelerationStructureDescriptor new];

      switch (type) {
      case MTL_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL:
         desc.type = MTLAccelerationStructureTypeBottomLevel;
         break;
      case MTL_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL:
         desc.type = MTLAccelerationStructureTypeTopLevel;
         break;
      case MTL_ACCELERATION_STRUCTURE_TYPE_GENERIC:
         desc.type = MTLAccelerationStructureTypeGeneric;
         break;
      }

      NSUInteger usage = MTLResourceUsageRayTracing;
      id<MTLAccelerationStructure> as = [dev newAccelerationStructureWithDescriptor:desc
                                                                       compactSize:&size];
      return (mtl_acceleration_structure *)as;
   }
}

mtl_acceleration_structure_command_encoder *
mtl_new_acceleration_structure_command_encoder(mtl_command_buffer *cmd_buffer)
{
   @autoreleasepool {
      id<MTL4CommandBuffer> cmd = (id<MTL4CommandBuffer>)cmd_buffer;
      return [[cmd accelerationStructureCommandEncoder] retain];
   }
}

void
mtl_acceleration_structure_encoder_build(mtl_acceleration_structure_command_encoder *encoder,
                                         const struct mtl_acceleration_structure_build_info *info)
{
   @autoreleasepool {
      id<MTL4AccelerationStructureCommandEncoder> enc = (id<MTL4AccelerationStructureCommandEncoder>)encoder;

      MTLAccelerationStructureBuildDescriptor *desc = [MTLAccelerationStructureBuildDescriptor new];
      desc.destinationAccelerationStructure = (id<MTLAccelerationStructure>)info->dst;
      desc.sourceAccelerationStructure = info->src ? (id<MTLAccelerationStructure>)info->src : nil;

      /* Translate geometry descriptors */
      if (info->geometry_count > 0 && info->geometries) {
         NSMutableArray *mtl_geometries = [NSMutableArray arrayWithCapacity:info->geometry_count];
         for (uint32_t i = 0; i < info->geometry_count; i++) {
            const struct mtl_acceleration_structure_geometry *geom = &info->geometries[i];
            MTLAccelerationStructureGeometryDescriptor *mtl_geom = [MTLAccelerationStructureGeometryDescriptor new];
            mtl_geom.type = (MTLAccelerationStructureGeometryType)geom->type;
            mtl_geom.flags = (MTLAccelerationStructureGeometryFlags)geom->flags;

            switch (geom->type) {
            case MTL_ACCELERATION_STRUCTURE_GEOMETRY_TYPE_TRIANGLES: {
               MTLAccelerationStructureTriangleGeometryDescriptor *tri_geom = [MTLAccelerationStructureTriangleGeometryDescriptor new];
               tri_geom.vertexFormat = (MTLVertexFormat)geom->triangles.vertex_format;
               tri_geom.vertexStride = geom->triangles.vertex_stride;
               tri_geom.vertexBuffer = (id<MTLBuffer>)geom->triangles.vertex_buffer;
               tri_geom.indexBuffer = geom->triangles.index_buffer ? (id<MTLBuffer>)geom->triangles.index_buffer : nil;
               tri_geom.indexType = (MTLIndexType)geom->triangles.index_type;
               tri_geom.vertexCount = geom->triangles.max_vertex + 1; /* max_vertex is max index, so count is max+1 */
               if (geom->triangles.transform_data)
                  tri_geom.transformBuffer = (id<MTLBuffer>)geom->triangles.transform_data;
               mtl_geom = tri_geom;
               break;
            }
            case MTL_ACCELERATION_STRUCTURE_GEOMETRY_TYPE_AABBS: {
               MTLAccelerationStructureAABBGeometryDescriptor *aabb_geom = [MTLAccelerationStructureAABBGeometryDescriptor new];
               aabb_geom.aabbBuffer = (id<MTLBuffer>)geom->aabbs.aabb_buffer;
               aabb_geom.stride = geom->aabbs.stride;
               mtl_geom = aabb_geom;
               break;
            }
            case MTL_ACCELERATION_STRUCTURE_GEOMETRY_TYPE_INSTANCES: {
               MTLAccelerationStructureInstanceGeometryDescriptor *inst_geom = [MTLAccelerationStructureInstanceGeometryDescriptor new];
               inst_geom.instanceBuffer = (id<MTLBuffer>)geom->instances.instance_buffer;
               inst_geom.instanceCount = geom->instances.instance_count;
               mtl_geom = inst_geom;
               break;
            }
            }

            [mtl_geometries addObject:mtl_geom];
         }
         desc.geometryDescriptors = [mtl_geometries copy];
      }

      if (info->ranges) {
         /* Build range descriptors */
         NSMutableArray *mtl_ranges = [NSMutableArray arrayWithCapacity:info->geometry_count];
         for (uint32_t i = 0; i < info->geometry_count; i++) {
            const struct mtl_acceleration_structure_build_range *range = &info->ranges[i];
            MTLAccelerationStructureBuildRangeDescriptor *mtl_range = [MTLAccelerationStructureBuildRangeDescriptor new];
            mtl_range.primitiveCount = range->primitive_count;
            mtl_range.primitiveOffset = range->primitive_offset;
            mtl_range.firstVertex = range->first_vertex;
            mtl_range.transformOffset = range->transform_offset;
            [mtl_ranges addObject:mtl_range];
         }
         desc.buildRangeDescriptors = [mtl_ranges copy];
      }

      desc.flags = (MTLAccelerationStructureBuildFlags)info->flags;
      desc.mode = (MTLAccelerationStructureBuildMode)info->mode;

      [enc buildAccelerationStructure:desc];
   }
}

void
mtl_acceleration_structure_encoder_copy(mtl_acceleration_structure_command_encoder *encoder,
                                        mtl_acceleration_structure *dst,
                                        mtl_acceleration_structure *src,
                                        enum mtl_copy_acceleration_structure_mode mode)
{
   @autoreleasepool {
      id<MTL4AccelerationStructureCommandEncoder> enc = (id<MTL4AccelerationStructureCommandEncoder>)encoder;

      MTLCopyAccelerationStructureMode mtl_mode;
      switch (mode) {
      case MTL_COPY_ACCELERATION_STRUCTURE_MODE_CLONE:
         mtl_mode = MTLCopyAccelerationStructureModeClone;
         break;
      case MTL_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT:
         mtl_mode = MTLCopyAccelerationStructureModeCompact;
         break;
      case MTL_COPY_ACCELERATION_STRUCTURE_MODE_SERIALIZE:
         mtl_mode = MTLCopyAccelerationStructureModeSerialize;
         break;
      case MTL_COPY_ACCELERATION_STRUCTURE_MODE_DESERIALIZE:
         mtl_mode = MTLCopyAccelerationStructureModeDeserialize;
         break;
      }

      [enc copyAccelerationStructure:(id<MTLAccelerationStructure>)dst
                        sourceAccelerationStructure:(id<MTLAccelerationStructure>)src
                                                     mode:mtl_mode];
   }
}

void
mtl_acceleration_structure_encoder_copy_to_buffer(mtl_acceleration_structure_command_encoder *encoder,
                                                  mtl_acceleration_structure *src,
                                                  void *dst)
{
   @autoreleasepool {
      id<MTL4AccelerationStructureCommandEncoder> enc = (id<MTL4AccelerationStructureCommandEncoder>)encoder;
      [enc copyAccelerationStructureToBuffer:(id<MTLAccelerationStructure>)src
                                      destinationBuffer:(id<MTLBuffer>)dst];
   }
}

void
mtl_acceleration_structure_encoder_copy_from_buffer(mtl_acceleration_structure_command_encoder *encoder,
                                                    mtl_acceleration_structure *dst,
                                                    const void *src)
{
   @autoreleasepool {
      id<MTL4AccelerationStructureCommandEncoder> enc = (id<MTL4AccelerationStructureCommandEncoder>)encoder;
      [enc copyBuffer:(id<MTLBuffer>)src
    destinationAccelerationStructure:(id<MTLAccelerationStructure>)dst];
   }
}

uint64_t
mtl_acceleration_structure_get_gpu_address(mtl_acceleration_structure *as)
{
   @autoreleasepool {
      id<MTLAccelerationStructure> mtl_as = (id<MTLAccelerationStructure>)as;
      return mtl_as.gpuResourceID;
   }
}

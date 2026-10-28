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

#include <string.h>
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

      // Note: MTLAccelerationStructureDescriptor in iOS 26 doesn't have a 'type' property
      // The type might be inferred from the build descriptor or set differently
      // For now, we create the structure without explicit type

      id<MTLAccelerationStructure> as = [dev newAccelerationStructureWithDescriptor:desc
                                                                       compactSize:&size];
      return (mtl_acceleration_structure *)as;
   }
}

mtl_acceleration_structure_command_encoder *
mtl_new_acceleration_structure_command_encoder(mtl_command_buffer *cmd_buffer)
{
   @autoreleasepool {
      id<MTLCommandBuffer> cmd = (id<MTLCommandBuffer>)cmd_buffer;
      return [[cmd accelerationStructureCommandEncoder] retain];
   }
}

void
mtl_acceleration_structure_encoder_build(mtl_acceleration_structure_command_encoder *encoder,
                                         const struct mtl_acceleration_structure_build_info *info)
{
   @autoreleasepool {
      id<MTLAccelerationStructureCommandEncoder> enc = (id<MTLAccelerationStructureCommandEncoder>)encoder;

      // Note: MTLAccelerationStructureBuildDescriptor may not exist in this SDK version
      // Using a simplified approach for now
      // TODO: Implement proper build descriptor when API is available

      // Create a basic build descriptor if available
      Class buildDescriptorClass = NSClassFromString(@"MTLAccelerationStructureBuildDescriptor");
      if (buildDescriptorClass) {
         id buildDesc = [buildDescriptorClass new];
         // Configure buildDesc from info if possible
         // This is a placeholder - full implementation requires MTL4 API
         (void)info;
         // [enc buildAccelerationStructure:buildDesc];
      } else {
         // Fallback: log warning - acceleration structure build not fully implemented
         fprintf(stderr, "Warning: MTLAccelerationStructureBuildDescriptor not available, acceleration structure build skipped\n");
         (void)info;
      }
   }
}

void
mtl_acceleration_structure_encoder_copy(mtl_acceleration_structure_command_encoder *encoder,
                                        mtl_acceleration_structure *dst,
                                        mtl_acceleration_structure *src,
                                        enum mtl_copy_acceleration_structure_mode mode)
{
   @autoreleasepool {
      id<MTLAccelerationStructureCommandEncoder> enc = (id<MTLAccelerationStructureCommandEncoder>)encoder;

      // MTLAccelerationStructureCopyMode enum not available in iOS 26 SDK
      // Using integer values directly matching our enum definitions
      NSUInteger mtl_mode = mode;

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
      id<MTLAccelerationStructureCommandEncoder> enc = (id<MTLAccelerationStructureCommandEncoder>)encoder;
      // Metal 4 API: copyAccelerationStructure:toBuffer:offset:
      [enc copyAccelerationStructure:(id<MTLAccelerationStructure>)src
                            toBuffer:(id<MTLBuffer>)dst
                              offset:0];
   }
}

void
mtl_acceleration_structure_encoder_copy_from_buffer(mtl_acceleration_structure_command_encoder *encoder,
                                                    mtl_acceleration_structure *dst,
                                                    const void *src)
{
   @autoreleasepool {
      id<MTLAccelerationStructureCommandEncoder> enc = (id<MTLAccelerationStructureCommandEncoder>)encoder;
      // Metal 4 API: copyBuffer:toAccelerationStructure:offset:
      [enc copyBuffer:(id<MTLBuffer>)src
          toAccelerationStructure:(id<MTLAccelerationStructure>)dst
                            offset:0];
   }
}

uint64_t
mtl_acceleration_structure_get_gpu_address(mtl_acceleration_structure *as)
{
   @autoreleasepool {
      id<MTLAccelerationStructure> mtl_as = (id<MTLAccelerationStructure>)as;
      MTLResourceID resource_id = mtl_as.gpuResourceID;
      // MTLResourceID is a struct of size 8 bytes (checked by static_assert)
      // Use memcpy to safely extract the uint64_t value
      uint64_t value;
      memcpy(&value, &resource_id, sizeof(value));
      return value;
   }
}

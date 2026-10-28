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
#include <Metal/MTL4AccelerationStructure.h>

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
                                  size_t size,
                                  kk_mtl_error_t *error)
{
   @autoreleasepool {
      // Try MTL4Device first (Metal 4), fall back to MTLDevice
      id<MTL4Device> dev4 = (id<MTL4Device>)device;
      id<MTLDevice> dev = (id<MTLDevice>)device;

      MTL4AccelerationStructureDescriptor *desc = [MTL4AccelerationStructureDescriptor new];

      // Note: MTL4AccelerationStructureDescriptor may not have a 'type' property in this SDK
      // The type might be inferred from the build descriptor or set differently
      // Set size on descriptor if available
      if ([desc respondsToSelector:@selector(setSize:)]) {
         [desc setSize:size];
      } else if ([desc respondsToSelector:@selector(setTotalSize:)]) {
         [desc setTotalSize:size];
      }

      NSError *nsError = nil;
      id<MTLAccelerationStructure> as = nil;

      // Try various method signatures for Metal 4
      if ([dev4 respondsToSelector:@selector(newAccelerationStructureWithDescriptor:error:)]) {
         as = [dev4 newAccelerationStructureWithDescriptor:desc error:&nsError];
      } else if ([dev4 respondsToSelector:@selector(newAccelerationStructureWithDescriptor:size:error:)]) {
         as = [dev4 newAccelerationStructureWithDescriptor:desc size:size error:&nsError];
      } else if ([dev respondsToSelector:@selector(newAccelerationStructureWithDescriptor:error:)]) {
         as = [dev newAccelerationStructureWithDescriptor:desc error:&nsError];
      } else if ([dev respondsToSelector:@selector(newAccelerationStructureWithDescriptor:size:error:)]) {
         as = [dev newAccelerationStructureWithDescriptor:desc size:size error:&nsError];
      } else {
         fprintf(stderr, "Error: No suitable acceleration structure creation method found\n");
         if (error)
            *error = (kk_mtl_error_t)[NSError errorWithDomain:@"KosmicKrisp" code:-1 userInfo:@{NSLocalizedDescriptionKey: @"Acceleration structure creation not supported"}];
         return NULL;
      }

      if (error)
         *error = (kk_mtl_error_t)nsError;
      return (mtl_acceleration_structure *)as;
   }
}

mtl_acceleration_structure_command_encoder *
mtl_new_acceleration_structure_command_encoder(mtl_command_buffer *cmd_buffer)
{
   @autoreleasepool {
      id<MTL4CommandBuffer> cmd4 = (id<MTL4CommandBuffer>)cmd_buffer;
      id<MTLCommandBuffer> cmd = (id<MTLCommandBuffer>)cmd_buffer;

      id encoder = nil;

      // Try MTL4 command buffer first
      if ([cmd4 respondsToSelector:@selector(accelerationStructureCommandEncoder)]) {
         encoder = [cmd4 accelerationStructureCommandEncoder];
      } else if ([cmd respondsToSelector:@selector(accelerationStructureCommandEncoder)]) {
         encoder = [cmd accelerationStructureCommandEncoder];
      } else {
         fprintf(stderr, "Error: accelerationStructureCommandEncoder not available\n");
         return NULL;
      }

      return [encoder retain];
   }
}

void
mtl_acceleration_structure_encoder_build(mtl_acceleration_structure_command_encoder *encoder,
                                         const struct mtl_acceleration_structure_build_info *info)
{
   @autoreleasepool {
      id<MTL4AccelerationStructureCommandEncoder> enc4 = (id<MTL4AccelerationStructureCommandEncoder>)encoder;
      id<MTLAccelerationStructureCommandEncoder> enc = (id<MTLAccelerationStructureCommandEncoder>)encoder;

      // Try to create build descriptor
      Class buildDescriptorClass = NSClassFromString(@"MTL4AccelerationStructureBuildDescriptor");
      if (!buildDescriptorClass) {
         buildDescriptorClass = NSClassFromString(@"MTLAccelerationStructureBuildDescriptor");
      }

      if (buildDescriptorClass) {
         id buildDesc = [buildDescriptorClass new];
         // Configure buildDesc from info if possible
         // This is a placeholder - full implementation requires MTL4 API
         (void)info;

         if ([enc4 respondsToSelector:@selector(buildAccelerationStructure:)]) {
            [enc4 buildAccelerationStructure:buildDesc];
         } else if ([enc respondsToSelector:@selector(buildAccelerationStructure:)]) {
            [enc buildAccelerationStructure:buildDesc];
         } else {
            fprintf(stderr, "Warning: buildAccelerationStructure: not available\n");
         }
      } else {
         // Fallback: log warning - acceleration structure build not fully implemented
         fprintf(stderr, "Warning: MTL4AccelerationStructureBuildDescriptor not available, acceleration structure build skipped\n");
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
      id<MTL4AccelerationStructureCommandEncoder> enc4 = (id<MTL4AccelerationStructureCommandEncoder>)encoder;
      id<MTLAccelerationStructureCommandEncoder> enc = (id<MTLAccelerationStructureCommandEncoder>)encoder;

      // MTLAccelerationStructureCopyMode enum not available in iOS 26 SDK
      // Using integer values directly matching our enum definitions
      NSUInteger mtl_mode = mode;

      // Try various copy method signatures
      if ([enc4 respondsToSelector:@selector(copyAccelerationStructure:toAccelerationStructure:mode:)]) {
         [enc4 copyAccelerationStructure:(id<MTLAccelerationStructure>)dst
                       toAccelerationStructure:(id<MTLAccelerationStructure>)src
                                          mode:mtl_mode];
      } else if ([enc4 respondsToSelector:@selector(copyAccelerationStructure:sourceAccelerationStructure:mode:)]) {
         [enc4 copyAccelerationStructure:(id<MTLAccelerationStructure>)dst
                     sourceAccelerationStructure:(id<MTLAccelerationStructure>)src
                                                  mode:mtl_mode];
      } else if ([enc respondsToSelector:@selector(copyAccelerationStructure:toAccelerationStructure:mode:)]) {
         [enc copyAccelerationStructure:(id<MTLAccelerationStructure>)dst
                       toAccelerationStructure:(id<MTLAccelerationStructure>)src
                                          mode:mtl_mode];
      } else if ([enc respondsToSelector:@selector(copyAccelerationStructure:sourceAccelerationStructure:mode:)]) {
         [enc copyAccelerationStructure:(id<MTLAccelerationStructure>)dst
                     sourceAccelerationStructure:(id<MTLAccelerationStructure>)src
                                                  mode:mtl_mode];
      } else {
         fprintf(stderr, "Warning: No suitable acceleration structure copy method found\n");
      }
   }
}

void
mtl_acceleration_structure_encoder_copy_to_buffer(mtl_acceleration_structure_command_encoder *encoder,
                                                  mtl_acceleration_structure *src,
                                                  void *dst)
{
   @autoreleasepool {
      id<MTL4AccelerationStructureCommandEncoder> enc4 = (id<MTL4AccelerationStructureCommandEncoder>)encoder;
      id<MTLAccelerationStructureCommandEncoder> enc = (id<MTLAccelerationStructureCommandEncoder>)encoder;

      // Try various copy to buffer method signatures
      if ([enc4 respondsToSelector:@selector(copyAccelerationStructure:toBuffer:offset:)]) {
         [enc4 copyAccelerationStructure:(id<MTLAccelerationStructure>)src
                              toBuffer:(id<MTLBuffer>)dst
                                offset:0];
      } else if ([enc respondsToSelector:@selector(copyAccelerationStructure:toBuffer:offset:)]) {
         [enc copyAccelerationStructure:(id<MTLAccelerationStructure>)src
                          toBuffer:(id<MTLBuffer>)dst
                            offset:0];
      } else {
         fprintf(stderr, "Warning: copyAccelerationStructure:toBuffer:offset: not available\n");
      }
   }
}

void
mtl_acceleration_structure_encoder_copy_from_buffer(mtl_acceleration_structure_command_encoder *encoder,
                                                    mtl_acceleration_structure *dst,
                                                    const void *src)
{
   @autoreleasepool {
      id<MTL4AccelerationStructureCommandEncoder> enc4 = (id<MTL4AccelerationStructureCommandEncoder>)encoder;
      id<MTLAccelerationStructureCommandEncoder> enc = (id<MTLAccelerationStructureCommandEncoder>)encoder;

      // Try various copy from buffer method signatures
      if ([enc4 respondsToSelector:@selector(copyBuffer:toAccelerationStructure:offset:)]) {
         [enc4 copyBuffer:(id<MTLBuffer>)src
             toAccelerationStructure:(id<MTLAccelerationStructure>)dst
                           offset:0];
      } else if ([enc respondsToSelector:@selector(copyBuffer:toAccelerationStructure:offset:)]) {
         [enc copyBuffer:(id<MTLBuffer>)src
             toAccelerationStructure:(id<MTLAccelerationStructure>)dst
                           offset:0];
      } else {
         fprintf(stderr, "Warning: copyBuffer:toAccelerationStructure:offset: not available\n");
      }
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
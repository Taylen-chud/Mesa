/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_device.h"
#include "kk_physical_device.h"
#include "kk_shader.h"

#include "kosmickrisp/clc/kk_precompiled_shader.h"

#include "libkk_shaders.h"

#include "kosmickrisp/bridge/mtl_bridge.h"

#include "util/u_atomic.h"

#include "nir/nir.h"
#include "nir/nir_builder.h"

static VkResult
build_precompiled_shader(struct kk_device *dev, enum libkk_program idx)
{
   struct kk_physical_device *pdev = kk_device_physical(dev);

   const uint32_t *bin = libkk_AppleSilicon[idx];
   const struct kk_precompiled_info *info = (void *)bin;
   const char *msl = (const char *)bin + sizeof(*info);

   mtl_library *library = mtl_new_library(
      dev->mtl_compiler_handle, msl, pdev->info.msl_version, MTL_MATH_MODE_FAST,
      MTL_MATH_FLOATING_POINT_FUNCTIONS_FAST);
   if (library == NULL)
      return vk_error(dev, VK_ERROR_INVALID_SHADER_NV);

   struct kk_precompiled_shader *shader = &dev->precompiled_cache.shaders[idx];

   /* TODO_KOSMICKRISP Do not hardcode the entrypoint */
   mtl_function_descriptor *function =
      mtl_new_library_function_descriptor(library, "main_entrypoint");
   uint32_t local_size_threads = info->workgroup_size[0] *
                                 info->workgroup_size[1] *
                                 info->workgroup_size[2];
   shader->pipeline = mtl_new_compute_pipeline_state(
      dev->mtl_compiler_handle, function, local_size_threads);
   mtl_release(function);
   mtl_release(library);

   if (!shader->pipeline)
      return vk_error(dev, VK_ERROR_INVALID_SHADER_NV);

   memcpy(&shader->info, info, sizeof(*info));
   return VK_SUCCESS;
}

struct kk_precompiled_shader *
kk_device_get_precompiled(struct kk_device *dev, enum libkk_program idx)
{
   struct kk_precompiled_shader *shader = &dev->precompiled_cache.shaders[idx];

   if (likely(p_atomic_read(&shader->ready)))
      return shader;

   simple_mtx_lock(&dev->precompiled_cache.lock);
   if (!p_atomic_read(&shader->ready)) {
      if (build_precompiled_shader(dev, idx) == VK_SUCCESS)
         p_atomic_set(&shader->ready, 1);
   }
   bool ready = p_atomic_read(&shader->ready);
   simple_mtx_unlock(&dev->precompiled_cache.lock);

   return ready ? shader : NULL;
}

VkResult
kk_device_init_lib(struct kk_device *dev)
{
   simple_mtx_init(&dev->precompiled_cache.lock, mtx_plain);
   return VK_SUCCESS;
}

void
kk_device_finish_lib(struct kk_device *dev)
{
   for (uint32_t i = 0; i < LIBKK_NUM_PROGRAMS; ++i) {
      if (dev->precompiled_cache.shaders[i].pipeline)
         mtl_release(dev->precompiled_cache.shaders[i].pipeline);
   }
   simple_mtx_destroy(&dev->precompiled_cache.lock);
}

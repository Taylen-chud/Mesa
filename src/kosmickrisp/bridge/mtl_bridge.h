/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_BRIDGE_H
#define KK_BRIDGE_H 1

/* C wrappers for Metal. May not be complete. If you find something you need
 * feel free to add them where they belong. As a rule of thumb, member functions
 * go in the objects' .h/.m/.c Naming convention for wrappers is:
 * object_type* mtl_new_object_type(params...);
 * void mtl_member_function(object_type* ptr, params...);
 * void mtl_object_set_member(object_type* ptr, member_type value);
 * member_type mtl_object_get_member(object_type* ptr);
 *
 * Functions that have new in the name require to release the returned object
 * via mtl_release(object);
 * */

#include "mtl_types.h"

#include "mtl_argument_table.h"
#include "mtl_buffer.h"
#include "mtl_command_buffer.h"
#include "mtl_command_queue.h"
#include "mtl_compiler.h"
#include "mtl_device.h"
#include "mtl_encoder.h"
#include "mtl_format.h"
#include "mtl_heap.h"
#include "mtl_render_state.h"
#include "mtl_residency_set.h"
#include "mtl_sampler.h"
#include "mtl_sync.h"
#include "mtl_texture.h"

mtl_acceleration_structure *mtl_acceleration_structure_create(mtl_device *device,
                                                              enum mtl_acceleration_structure_type type,
                                                              size_t size);
void mtl_acceleration_structure_encoder_build(mtl_acceleration_structure_command_encoder *encoder,
                                              const struct mtl_acceleration_structure_build_info *info);
void mtl_acceleration_structure_encoder_copy(mtl_acceleration_structure_command_encoder *encoder,
                                             mtl_acceleration_structure *dst,
                                             mtl_acceleration_structure *src,
                                             enum mtl_copy_acceleration_structure_mode mode);
void mtl_acceleration_structure_encoder_copy_to_buffer(mtl_acceleration_structure_command_encoder *encoder,
                                                       mtl_acceleration_structure *src,
                                                       void *dst);
void mtl_acceleration_structure_encoder_copy_from_buffer(mtl_acceleration_structure_command_encoder *encoder,
                                                         mtl_acceleration_structure *dst,
                                                         const void *src);
mtl_acceleration_structure_command_encoder *mtl_new_acceleration_structure_command_encoder(mtl_command_buffer *cmd_buffer);
uint64_t mtl_acceleration_structure_get_gpu_address(mtl_acceleration_structure *as);

mtl_texture *mtl_drawable_get_texture(void *drawable_ptr);

void *mtl_retain(void *handle);
void mtl_release(void *handle);

#endif /* KK_BRIDGE_H */

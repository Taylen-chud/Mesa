/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_shader.h"

#include "kk_cmd_buffer.h"
#include "kk_debug.h"
#include "kk_descriptor_set_layout.h"
#include "kk_device.h"
#include "kk_format.h"
#include "kk_nir_lower_vbo.h"
#include "kk_physical_device.h"
#include "kk_sampler.h"

#include "kosmickrisp/bridge/mtl_bridge.h"
#include "kosmickrisp/bridge/ns_process_info.h"
#include "kosmickrisp/bridge/vk_to_mtl_map.h"
#include "kosmickrisp/compiler/nir_to_msl.h"

#include "util/hash_table.h"
#include "util/ralloc.h"
#include "util/simple_mtx.h"

#include "nir_builder.h"
#include "nir_lower_blend.h"

#include "poly/nir/poly_nir.h"

#include "vk_blend.h"
#include "vk_format.h"
#include "vk_graphics_state.h"
#include "vk_nir_convert_ycbcr.h"
#include "vk_pipeline.h"

static const nir_shader_compiler_options *
kk_get_nir_options(struct vk_physical_device *vk_pdev, mesa_shader_stage stage,
                   UNUSED const struct vk_pipeline_robustness_state *rs)
{
   return &kk_nir_options;
}

static const struct vk_pipeline_robustness_state rs_all_supported = {
   .uniform_buffers =
      VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2,
   .storage_buffers =
      VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2,
   .images = VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_ROBUST_IMAGE_ACCESS_2,
};

static struct spirv_to_nir_options
kk_get_spirv_options(struct vk_physical_device *vk_pdev,
                     UNUSED mesa_shader_stage stage,
                     const struct vk_pipeline_robustness_state *rs)
{
   if (KK_DEBUG(FORCE_ROBUSTNESS))
      rs = &rs_all_supported;
   return (struct spirv_to_nir_options){
      .environment = NIR_SPIRV_VULKAN,
      .ssbo_addr_format = kk_buffer_addr_format(rs->storage_buffers),
      .phys_ssbo_addr_format = nir_address_format_64bit_global,
      .ubo_addr_format = kk_buffer_addr_format(rs->uniform_buffers),
      .shared_addr_format = nir_address_format_32bit_offset,
      .min_ssbo_alignment = KK_MIN_SSBO_ALIGNMENT,
      .min_ubo_alignment = KK_MIN_UBO_ALIGNMENT,
   };
}

static void
kk_preprocess_nir(UNUSED struct vk_physical_device *vk_pdev, nir_shader *nir,
                  UNUSED const struct vk_pipeline_robustness_state *rs)
{
   /* Gather info before preprocess_nir but after some general lowering, so
    * inputs_read and system_values_read are accurately set.
    */
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   msl_preprocess_nir(nir);

   /* Cannot be part of msl_preprocess_nir since clc does not expose
    * has_base_workgroup_id */
   nir_lower_compute_system_values_options csv_options = {
      .has_base_global_invocation_id = 0,
      .has_base_workgroup_id = true,
   };
   NIR_PASS(_, nir, nir_lower_compute_system_values, &csv_options);
}

static bool
has_static_depth_stencil_state(const struct vk_graphics_pipeline_state *state)
{
   if (!state->ds)
      return false;

   return !(
      BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_DS_DEPTH_TEST_ENABLE) |
      BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_DS_DEPTH_WRITE_ENABLE) |
      BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_DS_DEPTH_COMPARE_OP) |
      BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_DS_STENCIL_TEST_ENABLE) |
      BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_DS_STENCIL_OP) |
      BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_DS_STENCIL_COMPARE_MASK) |
      BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_DS_STENCIL_WRITE_MASK));
}

struct kk_vs_key {
   bool is_points;
   struct vk_vertex_input_state vi;
};

static void
kk_populate_vs_key(struct kk_vs_key *key,
                   const struct vk_graphics_pipeline_state *state)
{
   memset(key, 0, sizeof(*key));
   key->is_points =
      (state->ia->primitive_topology == VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
   key->vi = *state->vi;
}

struct kk_fs_key {
   VkFormat color_formats[MESA_VK_MAX_COLOR_ATTACHMENTS];
   struct vk_color_blend_state color_blend;
   struct vk_depth_stencil_state ds;
   uint32_t rasterization_samples;
   uint16_t static_sample_mask;
   bool sample_shading_enable;
   bool has_depth;
   bool has_stencil;
   bool dynamic_input_attachment_map;
   uint8_t depth_input_att;
   uint8_t stencil_input_att;
};

static void
kk_populate_fs_key(struct kk_fs_key *key,
                   const struct vk_graphics_pipeline_state *state)
{
   memset(key, 0, sizeof(*key));

   /* Required since we [de]serialize blend, and render target swizzle for
    * non-native formats */
   memcpy(key->color_formats, state->rp->color_attachment_formats,
          sizeof(key->color_formats));

   if (has_static_depth_stencil_state(state))
      key->ds = *(state->ds);

   /* Blend state gets [de]serialized, so we need to hash it */
   if (state->cb)
      key->color_blend = *(state->cb);

   if (state->ms) {
      key->rasterization_samples = state->ms->rasterization_samples;
      key->static_sample_mask = state->ms->sample_mask;
      key->sample_shading_enable = state->ms->sample_shading_enable;
   }

   /* Depth writes are removed unless there's an actual attachment */
   key->has_depth = state->rp->depth_attachment_format != VK_FORMAT_UNDEFINED;
   key->has_stencil =
      state->rp->stencil_attachment_format != VK_FORMAT_UNDEFINED;

   /* Values that modify if a forced depth is present or not */
   key->depth_input_att =
      state->ial ? state->ial->depth_att : MESA_VK_ATTACHMENT_NO_INDEX;
   key->stencil_input_att =
      state->ial ? state->ial->stencil_att : MESA_VK_ATTACHMENT_NO_INDEX;
   key->dynamic_input_attachment_map =
      BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_INPUT_ATTACHMENT_MAP);
}

enum kk_feature_key {
   KK_FEAT_CUSTOM_BORDER = BITFIELD_BIT(0),
   KK_FEAT_NULL_DESCRIPTOR = BITFIELD_BIT(1),
   KK_FEAT_IMAGE_VIEW_MIN_LOD = BITFIELD_BIT(2),
};

static enum kk_feature_key
kk_make_feature_key(const struct vk_features *feats)
{
   enum kk_feature_key key = 0;
   if (feats->customBorderColors)
      key |= KK_FEAT_CUSTOM_BORDER;
   if (feats->nullDescriptor)
      key |= KK_FEAT_NULL_DESCRIPTOR;
   if (feats->minLod)
      key |= KK_FEAT_IMAGE_VIEW_MIN_LOD;
   return key;
}

static void
kk_hash_graphics_state(struct vk_physical_device *device,
                       const struct vk_graphics_pipeline_state *state,
                       const struct vk_features *enabled_features,
                       VkShaderStageFlags stages, blake3_hash blake3_out)
{
   struct mesa_blake3 blake3_ctx;
   _mesa_blake3_init(&blake3_ctx);

   if (stages & VK_SHADER_STAGE_VERTEX_BIT) {
      struct kk_vs_key key;
      kk_populate_vs_key(&key, state);
      _mesa_blake3_update(&blake3_ctx, &key, sizeof(key));
   }

   if (stages & VK_SHADER_STAGE_FRAGMENT_BIT) {
      struct kk_fs_key key;
      kk_populate_fs_key(&key, state);
      _mesa_blake3_update(&blake3_ctx, &key, sizeof(key));

      _mesa_blake3_update(&blake3_ctx, &state->mv->view_mask,
                          sizeof(state->mv->view_mask));
   }

   enum kk_feature_key feature_key = kk_make_feature_key(enabled_features);
   _mesa_blake3_update(&blake3_ctx, &feature_key, sizeof(feature_key));

   _mesa_blake3_final(&blake3_ctx, blake3_out);
}

static void
shared_var_info(const struct glsl_type *type, unsigned *size, unsigned *align)
{
   assert(glsl_type_is_vector_or_scalar(type));

   uint32_t comp_size =
      glsl_type_is_boolean(type) ? 4 : glsl_get_bit_size(type) / 8;
   unsigned length = glsl_get_vector_elements(type);
   *size = comp_size * length, *align = comp_size;
}

struct lower_ycbcr_state {
   uint32_t set_layout_count;
   struct vk_descriptor_set_layout *const *set_layouts;
};

static const struct vk_ycbcr_conversion_state *
lookup_ycbcr_conversion(const void *_state, uint32_t set, uint32_t binding,
                        uint32_t array_index)
{
   const struct lower_ycbcr_state *state = _state;
   assert(set < state->set_layout_count);
   assert(state->set_layouts[set] != NULL);
   const struct kk_descriptor_set_layout *set_layout =
      vk_to_kk_descriptor_set_layout(state->set_layouts[set]);
   assert(binding < set_layout->binding_count);

   const struct kk_descriptor_set_binding_layout *bind_layout =
      &set_layout->binding[binding];

   if (bind_layout->immutable_samplers == NULL)
      return NULL;

   array_index = MIN2(array_index, bind_layout->array_size - 1);

   const struct kk_sampler *sampler =
      bind_layout->immutable_samplers[array_index];

   return sampler && sampler->vk.ycbcr_conversion
             ? &sampler->vk.ycbcr_conversion->state
             : NULL;
}

static unsigned
type_size_vec4(const struct glsl_type *type, bool bindless)
{
   return glsl_count_attribute_slots(type, false);
}

static bool
kk_nir_swizzle_fragment_output(nir_builder *b, nir_intrinsic_instr *intrin,
                               void *data)
{
   if (intrin->intrinsic != nir_intrinsic_store_output &&
       intrin->intrinsic != nir_intrinsic_load_output)
      return false;

   unsigned slot = nir_intrinsic_io_semantics(intrin).location;
   if (slot < FRAG_RESULT_DATA0)
      return false;

   const struct vk_graphics_pipeline_state *state =
      (const struct vk_graphics_pipeline_state *)data;
   VkFormat vk_format =
      state->rp->color_attachment_formats[slot - FRAG_RESULT_DATA0];
   if (vk_format == VK_FORMAT_UNDEFINED)
      return false;

   enum pipe_format format = vk_format_to_pipe_format(vk_format);
   const struct kk_va_format *supported_format = kk_get_va_format(format);

   /* Check if we have to apply any swizzle */
   if (!supported_format->is_native) {
      unsigned channel_unswizzle[] = {
         supported_format->unswizzle.red, supported_format->unswizzle.green,
         supported_format->unswizzle.blue, supported_format->unswizzle.alpha};

      if (intrin->intrinsic == nir_intrinsic_store_output) {
         unsigned channel_swizzle[4] = {0u};
         for (uint32_t i = 0u; i < 4; ++i)
            channel_swizzle[channel_unswizzle[i]] = i;

         b->cursor = nir_before_instr(&intrin->instr);
         nir_def *to_replace = intrin->src[0].ssa;
         nir_def *swizzled = nir_swizzle(b, to_replace, channel_swizzle,
                                         to_replace->num_components);
         nir_src_rewrite(&intrin->src[0], swizzled);
      } else {
         b->cursor = nir_after_instr(&intrin->instr);
         nir_def *to_replace = &intrin->def;
         nir_def *swizzled = nir_swizzle(b, to_replace, channel_unswizzle,
                                         to_replace->num_components);
         nir_def_rewrite_uses_after(to_replace, swizzled);
      }
      return true;
   }

   return false;
}

static bool
kk_is_possible_both_depth_clip_clamp(
   const struct vk_graphics_pipeline_state *state, bool enabled)
{
   bool dyn_clamp =
      BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_RS_DEPTH_CLAMP_ENABLE);
   bool dyn_clip =
      BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_RS_DEPTH_CLIP_ENABLE);

   /* If rasterization state is null, clamp must be dynamic, and clip must be
    * inverted from clamp if not also dynamic. Thus, they cannot match unless
    * both are dynamic. */
   if (!state->rs)
      return dyn_clamp && dyn_clip;

   /* If clip is static and inverted from clamp, they can never be the same */
   if (!dyn_clip &&
       state->rs->depth_clip_enable == VK_MESA_DEPTH_CLIP_ENABLE_NOT_CLAMP)
      return false;

   /* Need to account for:
    * - Both are dynamic
    * - Both are static and match the desired value
    * - One is dynamic and the other is static and matches the desired value
    */
   bool static_clamp_match = state->rs->depth_clamp_enable == enabled;
   bool static_clip_match =
      vk_rasterization_state_depth_clip_enable(state->rs) == enabled;
   return (dyn_clamp || static_clamp_match) && (dyn_clip || static_clip_match);
}

static void
kk_lower_vs_vbo(nir_shader *nir, const struct vk_graphics_pipeline_state *state,
                const struct vk_pipeline_robustness_state *rs)
{
   assert(!(nir->info.inputs_read & BITFIELD64_MASK(VERT_ATTRIB_GENERIC0)) &&
          "Fixed-function attributes not used in Vulkan");
   NIR_PASS(_, nir, nir_recompute_io_bases, nir_var_shader_in);
   /* the shader_out portion of this is load-bearing even for tess eval */
   /* Fold constant offset srcs for IO. */
   NIR_PASS(_, nir, nir_opt_constant_folding);

   struct kk_attribute attributes[KK_MAX_ATTRIBS] = {};
   uint64_t attribs_read = nir->info.inputs_read >> VERT_ATTRIB_GENERIC0;
   u_foreach_bit(i, state->vi->attributes_valid) {
      const struct vk_vertex_attribute_state *attr = &state->vi->attributes[i];
      assert(state->vi->bindings_valid & BITFIELD_BIT(attr->binding));
      const struct vk_vertex_binding_state *binding =
         &state->vi->bindings[attr->binding];

      /* nir_assign_io_var_locations compacts vertex inputs, eliminating
       * unused inputs. We need to do the same here to match the locations.
       */
      unsigned slot = util_bitcount64(attribs_read & BITFIELD_MASK(i));
      attributes[slot].divisor = binding->divisor;
      attributes[slot].binding = attr->binding;
      attributes[slot].format = vk_format_to_pipe_format(attr->format);
      attributes[slot].buf = attr->binding;
      attributes[slot].instanced =
         binding->input_rate == VK_VERTEX_INPUT_RATE_INSTANCE;
   }
   bool robustness2 =
      rs->vertex_inputs ==
      VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2;
   NIR_PASS(_, nir, kk_nir_lower_vbo, attributes, robustness2);
}

/* Lowering for the stage which ends up as the vertex stage on hardware */
static void
kk_lower_hw_vs(nir_shader *nir, const struct vk_graphics_pipeline_state *state,
               bool is_point)
{
   if (is_point)
      NIR_PASS(_, nir, msl_ensure_vertex_point_size_output);
   else
      nir_shader_intrinsics_pass(nir, msl_nir_vs_remove_point_size_write,
                                 nir_metadata_control_flow, NULL);

   NIR_PASS(_, nir, msl_ensure_vertex_position_output);
   NIR_PASS(_, nir, nir_lower_clip_halfz_dynamic);

   /* In any situation where both clip and clamp can be disabled, we need to
    * add viewport Z transform emulation to the shader */
   if (kk_is_possible_both_depth_clip_clamp(state, false))
      NIR_PASS(_, nir, msl_nir_lower_vs_disabled_depth_clamp_clip);

   NIR_PASS(_, nir, msl_nir_vs_io_types);
}

static inline uint8_t
kk_get_logical_color_att_index(const struct vk_graphics_pipeline_state *state,
                               uint8_t i)
{
   return (state->cal) ? state->cal->color_map[i] : i;
}

static void
kk_lower_fs_blend(nir_shader *nir,
                  const struct vk_graphics_pipeline_state *state)
{
   nir_lower_blend_options opts = {
      .scalar_blend_const = false,
      .logicop_enable = state->cb->logic_op_enable,
      .logicop_func = vk_logic_op_to_pipe(state->cb->logic_op),
   };

   static_assert(ARRAY_SIZE(opts.rt) == 8, "max RTs out of sync");
   for (unsigned i = 0; i < ARRAY_SIZE(opts.rt); ++i) {
      opts.rt[i].format = PIPE_FORMAT_NONE;
   }

   for (unsigned i = 0; i < ARRAY_SIZE(opts.rt); ++i) {
      uint8_t logical_index = kk_get_logical_color_att_index(state, i);
      if (logical_index == MESA_VK_ATTACHMENT_UNUSED) {
         continue;
      }

      nir_lower_blend_rt *rt = &opts.rt[logical_index];

      const struct vk_color_blend_attachment_state *att =
         &state->cb->attachments[i];
      rt->format =
         vk_format_to_pipe_format(state->rp->color_attachment_formats[i]);
      rt->colormask = att->write_mask;

      if (!att->blend_enable) {
         rt->rgb.src_factor = rt->alpha.src_factor = PIPE_BLENDFACTOR_ONE;
         rt->rgb.dst_factor = rt->alpha.dst_factor = PIPE_BLENDFACTOR_ZERO;
         rt->rgb.func = rt->alpha.func = PIPE_BLEND_ADD;
      } else if (att->color_blend_op >= VK_BLEND_OP_ZERO_EXT) {
         rt->advanced_blend = true;
         rt->blend_mode = vk_advanced_blend_op_to_pipe(att->color_blend_op);
         rt->src_premultiplied = att->src_premultiplied;
         rt->dst_premultiplied = att->dst_premultiplied;
         rt->overlap = vk_blend_overlap_to_pipe(att->blend_overlap);

         /* Unused by runtime and not supported */
         assert(!att->clamp_results);
      } else {
         rt->rgb.src_factor =
            vk_blend_factor_to_pipe(att->src_color_blend_factor);
         rt->rgb.dst_factor =
            vk_blend_factor_to_pipe(att->dst_color_blend_factor);
         rt->rgb.func = vk_blend_op_to_pipe(att->color_blend_op);

         rt->alpha.src_factor =
            vk_blend_factor_to_pipe(att->src_alpha_blend_factor);
         rt->alpha.dst_factor =
            vk_blend_factor_to_pipe(att->dst_alpha_blend_factor);
         rt->alpha.func = vk_blend_op_to_pipe(att->alpha_blend_op);
      }
   }
   /* Fold constant offset srcs for IO. */
   NIR_PASS(_, nir, nir_opt_constant_folding);
   NIR_PASS(_, nir, nir_lower_blend, &opts);
}

[... trimmed for brevity in this commit content ...]

static void
kk_msl_copy(struct msl_compile_data *dst, const struct msl_compile_data *src)
{
   dst->code = ralloc_strdup(NULL, src->code);
   dst->entrypoint_name = ralloc_strdup(NULL, src->entrypoint_name);
}

/* Copies all msl shader data to the vertex and gathers information for pipeline
 * compilation. */
static void
gather_graphics_pipeline_create_info(
   const struct vk_graphics_pipeline_state *state, struct kk_shader **shaders,
   uint32_t shader_count)
{
   assert(state && shader_count >= 2 &&
          "state, vertex and fragment shaders are a must");
   struct kk_shader *vs = shaders[0];
   struct kk_shader_info *info = &vs->info;

   /* Render pass data */
   const struct vk_render_pass_state *rp = state->rp;
   bool has_depth = rp->depth_attachment_format != VK_FORMAT_UNDEFINED;
   bool has_stencil = rp->stencil_attachment_format != VK_FORMAT_UNDEFINED;
   {
      info->vs.color_attachment_count = rp->color_attachment_count;
      for (uint8_t i = 0u; i < MAX_DRAW_BUFFERS; ++i) {
         info->vs.rt_formats[i] = MTL_PIXEL_FORMAT_INVALID;
      }
      for (uint8_t i = 0u; i < state->rp->color_attachment_count; ++i) {
         uint8_t logical_index = kk_get_logical_color_att_index(state, i);
         if (logical_index == MESA_VK_ATTACHMENT_UNUSED) {
            continue;
         }
         VkFormat format = state->rp->color_attachment_formats[i];
         info->vs.rt_formats[logical_index] =
            format == VK_FORMAT_UNDEFINED
               ? MTL_PIXEL_FORMAT_INVALID
               : vk_format_to_mtl_pixel_format(format);
      }
      info->vs.d_format =
         has_depth ? vk_format_to_mtl_pixel_format(rp->depth_attachment_format)
                   : MTL_PIXEL_FORMAT_INVALID;
      info->vs.s_format =
         has_stencil
            ? vk_format_to_mtl_pixel_format(rp->stencil_attachment_format)
            : MTL_PIXEL_FORMAT_INVALID;

      info->vs.view_mask = state->mv->view_mask;
   }

   info->vs.has_ds = has_static_depth_stencil_state(state);
   if (info->vs.has_ds)
      kk_gather_ds_info(state->ds, has_depth, has_stencil, info);

   info->vs.has_ms = state->ms != NULL;
   info->vs.sample_count = 1u;
   if (info->vs.has_ms) {
      const struct vk_multisample_state *ms = state->ms;
      info->vs.sample_count = ms->rasterization_samples;
   }

   /* We need to store all other stage sources in the vertex too otherwise we
    * won't be able to create the whole pipeline correctly. */
   struct kk_shader *gs = NULL;
   for (uint32_t i = 1; i < shader_count; ++i) {
      struct kk_shader *s = shaders[i];
      mesa_shader_stage stage = s->info.stage;
      kk_msl_copy(&vs->msl_data[stage], &s->msl_data[stage]);

      info->vs.additional_stages_bits |= BITFIELD_BIT(stage);

      if (s->info.stage == MESA_SHADER_TESS_CTRL)
         info->vs.tess_local_thread_size = s->info.tess.tcs_output_patch_size;

      if (s->info.stage == MESA_SHADER_GEOMETRY) {
         kk_msl_copy(&vs->gs_main, &s->gs_main);
         gs = s;
      }
   }

   uint8_t topology = state->ia->primitive_topology;
   if (info->vs.additional_stages_bits & BITFIELD_BIT(MESA_SHADER_TESS_CTRL)) {
      /* TODO_KOSMICKRISP Hackity hack */
      struct kk_shader *tesc = shaders[1];
      struct kk_shader *tese = shaders[2];

      struct kk_tess_info info =
         kk_tess_info_merge(tese->info.tess.info, tesc->info.tess.info);

      /* Determine primitive based on the merged state */
      if (info.points) {
         topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
      } else if (info.mode == TESS_PRIMITIVE_ISOLINES) {
         topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
      } else {
         topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      }
   }

   if (gs) {
      /* Only the topology class matters here */
      enum mesa_prim prim = u_reduced_prim(gs->info.gs.mode);
      if (prim == MESA_PRIM_POINTS)
         topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
      else if (prim == MESA_PRIM_LINES)
         topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
      else
         topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
   }

   info->vs.topology =
      vk_primitive_topology_to_mtl_primitive_topology_class(topology);
}

[... remainder unchanged ...]

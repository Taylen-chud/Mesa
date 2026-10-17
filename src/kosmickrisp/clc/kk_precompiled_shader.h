/*
 * Copyright 2026 LunarG, Inc.
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_PRECOMPILED_SHADER_H
#define KK_PRECOMPILED_SHADER_H 1

#include "kosmickrisp/bridge/mtl_types.h"

#include <stdint.h>
#include <signal.h>

#include "util/u_atomic.h"

/*
 * Some platforms/toolchains used by CI may not expose the project-specific
 * atomic typedef `p_atomic_t`. Provide a safe fallback so the header can be
 * built on those platforms (e.g. iOS toolchain used in CI). We prefer the
 * project typedef if present; otherwise fall back to `sig_atomic_t` which is
 * universally available and suitable for a simple ready flag.
 */
#ifndef p_atomic_t
typedef sig_atomic_t p_atomic_t;
#endif

struct kk_precompiled_info {
   uint16_t workgroup_size[3];
};

struct kk_precompiled_shader {
   struct kk_precompiled_info info;
   mtl_compute_pipeline_state *pipeline;
   p_atomic_t ready;
};

#endif /* KK_PRECOMPILED_SHADER_H_ */

/* SPDX-License-Identifier: MIT */

#include <linux/version.h>

#ifndef __BACKPORT_DRM_DP_H__
#define __BACKPORT_DRM_DP_H__

#include_next <drm/display/drm_dp.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,8,0)
// https://github.com/gregkh/linux/commit/b348150406564595cf6c1be388e9797fa97c2a5d
# define DP_HBLANK_EXPANSION_CAPABLE        (1 << 3)
# define DP_DSC_PASSTHROUGH_EN		    (1 << 1)
#endif

#endif /* __BACKPORT_DRM_DP_H__ */
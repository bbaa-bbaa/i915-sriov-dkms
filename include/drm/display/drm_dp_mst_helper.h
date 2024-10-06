/* SPDX-License-Identifier: MIT */

#ifndef __BACKPORT_DRM_DP_MST_HELPER_H__
#define __BACKPORT_DRM_DP_MST_HELPER_H__

#include <linux/version.h>
#include_next <drm/display/drm_dp_mst_helper.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
// https://github.com/gregkh/linux/commit/9dcf67deeab6fbc4984175278b1b2c59881dca52
bool drm_dp_mst_port_downstream_of_parent(struct drm_dp_mst_topology_mgr *mgr,
					  struct drm_dp_mst_port *port,
					  struct drm_dp_mst_port *parent);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
// https://github.com/gregkh/linux/commit/9dcf67deeab6fbc4984175278b1b2c59881dca52
int __must_check drm_dp_mst_atomic_check_mgr(struct drm_atomic_state *state,
					     struct drm_dp_mst_topology_mgr *mgr,
					     struct drm_dp_mst_topology_state *mst_state,
					     struct drm_dp_mst_port **failing_port);
#endif

#endif /* __BACKPORT_DRM_DP_MST_HELPER_H__ */

/* SPDX-License-Identifier: MIT */

#include <linux/version.h>

#ifndef __BACKPORT_DRM_PROPERTY_H__
#define __BACKPORT_DRM_PROPERTY_H__

#include_next <drm/drm_property.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,8,0)
// https://github.com/gregkh/linux/commit/601603105325ad4ec62db95c9bc428202ece2c8f
int drm_property_replace_blob_from_id(struct drm_device *dev,
				      struct drm_property_blob **blob,
				      uint64_t blob_id,
				      ssize_t expected_size,
				      ssize_t expected_elem_size,
				      bool *replaced);
#endif

#endif /* __BACKPORT_DRM_PROPERTY_H__ */
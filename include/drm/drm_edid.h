/* SPDX-License-Identifier: MIT */

#include <linux/version.h>

#ifndef __BACKPORT_DRM_EDID_H__
#define __BACKPORT_DRM_EDID_H__

#include_next <drm/drm_edid.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,7,0)
// https://github.com/gregkh/linux/commit/7218779efc46cdb48c1b9f959ea5cbb06333192f
bool drm_edid_is_digital(const struct drm_edid *drm_edid);
#endif

#endif /* __BACKPORT_DRM_EDID_H__ */
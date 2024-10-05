#ifndef __BACKPORT_DRM_MODE_H__
#define __BACKPORT_DRM_MODE_H__

#include_next <uapi/drm/drm_mode.h>

/*
 * Creating 64 bit palette entries for better data
 * precision. This will be required for HDR and
 * similar color processing usecases.
 */
struct drm_color_lut_ext {
	/*
	 * Data is U32.32 fixed point format.
	 */
	__u64 red;
	__u64 green;
	__u64 blue;
	__u64 reserved;
};

#endif /* __BACKPORT_DRM_MODE_H__ */
/*
 * Copyright (c) 2006 Luc Verhaegen (quirks list)
 * Copyright (c) 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 * Copyright 2010 Red Hat, Inc.
 *
 * DDC probing routines (drm_ddc_read & drm_do_probe_ddc_edid) originally from
 * FB layer.
 *   Copyright (C) 2006 Dennis Munsie <dmunsie@cecropia.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <linux/version.h>
#include <linux/types.h>

#include <drm/drm_edid.h>


/*
 * The opaque EDID type, internal to drm_edid.c.
 */
struct drm_edid {
	/* Size allocated for edid */
	size_t size;
	const struct edid *edid;
};

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,7,0)
// https://github.com/gregkh/linux/commit/7218779efc46cdb48c1b9f959ea5cbb06333192f
/**
 * drm_edid_is_digital - is digital?
 * @drm_edid: The EDID
 *
 * Return true if input is digital.
 */
bool drm_edid_is_digital(const struct drm_edid *drm_edid)
{
	return drm_edid && drm_edid->edid &&
		drm_edid->edid->input & DRM_EDID_INPUT_DIGITAL;
}
#endif
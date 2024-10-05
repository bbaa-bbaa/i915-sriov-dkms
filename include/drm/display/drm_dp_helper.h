/* SPDX-License-Identifier: MIT */

#ifndef __BACKPORT_DRM_DP_HELPER_H__
#define __BACKPORT_DRM_DP_HELPER_H__

#include <linux/version.h>
#include_next <drm/display/drm_dp_helper.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
// https://github.com/gregkh/linux/commit/0c2287c9652150cf659408b66c1789830822132f#diff-5e5d28187ab55a9d0880751852de5578d32411928048d8dbb3798d8a0a144cf3R167
u8 drm_dp_dsc_sink_bpp_incr(const u8 dsc_dpcd[DP_DSC_RECEIVER_CAP_SIZE]);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
// https://github.com/gregkh/linux/commit/d389989ed530b3d8944974b7ee866b089720bc9c
/**
 * drm_dp_is_uhbr_rate - Determine if a link rate is UHBR
 * @link_rate: link rate in 10kbits/s units
 *
 * Determine if the provided link rate is an UHBR rate.
 *
 * Returns: %True if @link_rate is an UHBR rate.
 */
static inline bool drm_dp_is_uhbr_rate(int link_rate)
{
	return link_rate >= 1000000;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
// https://github.com/gregkh/linux/commit/c1d6a22b7219bd52c66e9e038a282ba79f04be1f
#define DRM_DP_BW_OVERHEAD_MST		BIT(0)
#define DRM_DP_BW_OVERHEAD_UHBR		BIT(1)
#define DRM_DP_BW_OVERHEAD_SSC_REF_CLK	BIT(2)
#define DRM_DP_BW_OVERHEAD_FEC		BIT(3)
#define DRM_DP_BW_OVERHEAD_DSC		BIT(4)
int drm_dp_bw_overhead(int lane_count, int hactive,
		       int dsc_slice_count,
		       int bpp_x16, unsigned long flags);
int drm_dp_bw_channel_coding_efficiency(bool is_uhbr);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 7, 0)
// https://github.com/gregkh/linux/commit/113cdddcded6d597b64d824a59d0186db150113a
#ifdef CONFIG_DRM_DP_CEC
void drm_dp_cec_attach(struct drm_dp_aux *aux, u16 source_physical_address);
#else
static inline void drm_dp_cec_attach(struct drm_dp_aux *aux,
				     u16 source_physical_address)
{
}
#endif
#endif

#endif /* __DRM_DP_HELPER_H__ */

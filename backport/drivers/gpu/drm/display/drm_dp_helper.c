#include <linux/version.h>

#include <drm/display/drm_dp_helper.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
// https://github.com/gregkh/linux/commit/0c2287c9652150cf659408b66c1789830822132f
/**
 * drm_dp_dsc_sink_bpp_incr() - Get bits per pixel increment
 * @dsc_dpcd: DSC capabilities from DPCD
 *
 * Returns the bpp precision supported by the DP sink.
 */
u8 drm_dp_dsc_sink_bpp_incr(const u8 dsc_dpcd[DP_DSC_RECEIVER_CAP_SIZE])
{
	u8 bpp_increment_dpcd = dsc_dpcd[DP_DSC_BITS_PER_PIXEL_INC - DP_DSC_SUPPORT];
	switch (bpp_increment_dpcd) {
	case DP_DSC_BITS_PER_PIXEL_1_16:
		return 16;
	case DP_DSC_BITS_PER_PIXEL_1_8:
		return 8;
	case DP_DSC_BITS_PER_PIXEL_1_4:
		return 4;
	case DP_DSC_BITS_PER_PIXEL_1_2:
		return 2;
	case DP_DSC_BITS_PER_PIXEL_1_1:
		return 1;
	}
	return 0;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
/* See DP Standard v2.1 2.6.4.4.1.1, 2.8.4.4, 2.8.7 */
static int drm_dp_link_symbol_cycles(int lane_count, int pixels, int bpp_x16,
				     int symbol_size, bool is_mst)
{
	int cycles = DIV_ROUND_UP(pixels * bpp_x16, 16 * symbol_size * lane_count);
	int align = is_mst ? 4 / lane_count : 1;
	return ALIGN(cycles, align);
}
static int drm_dp_link_dsc_symbol_cycles(int lane_count, int pixels, int slice_count,
					 int bpp_x16, int symbol_size, bool is_mst)
{
	int slice_pixels = DIV_ROUND_UP(pixels, slice_count);
	int slice_data_cycles = drm_dp_link_symbol_cycles(lane_count, slice_pixels,
							  bpp_x16, symbol_size, is_mst);
	int slice_eoc_cycles = is_mst ? 4 / lane_count : 1;
	return slice_count * (slice_data_cycles + slice_eoc_cycles);
}
/**
 * drm_dp_bw_overhead - Calculate the BW overhead of a DP link stream
 * @lane_count: DP link lane count
 * @hactive: pixel count of the active period in one scanline of the stream
 * @dsc_slice_count: DSC slice count if @flags/DRM_DP_LINK_BW_OVERHEAD_DSC is set
 * @bpp_x16: bits per pixel in .4 binary fixed point
 * @flags: DRM_DP_OVERHEAD_x flags
 *
 * Calculate the BW allocation overhead of a DP link stream, depending
 * on the link's
 * - @lane_count
 * - SST/MST mode (@flags / %DRM_DP_OVERHEAD_MST)
 * - symbol size (@flags / %DRM_DP_OVERHEAD_UHBR)
 * - FEC mode (@flags / %DRM_DP_OVERHEAD_FEC)
 * - SSC/REF_CLK mode (@flags / %DRM_DP_OVERHEAD_SSC_REF_CLK)
 * as well as the stream's
 * - @hactive timing
 * - @bpp_x16 color depth
 * - compression mode (@flags / %DRM_DP_OVERHEAD_DSC).
 * Note that this overhead doesn't account for the 8b/10b, 128b/132b
 * channel coding efficiency, for that see
 * @drm_dp_link_bw_channel_coding_efficiency().
 *
 * Returns the overhead as 100% + overhead% in 1ppm units.
 */
int drm_dp_bw_overhead(int lane_count, int hactive,
		       int dsc_slice_count,
		       int bpp_x16, unsigned long flags)
{
	int symbol_size = flags & DRM_DP_BW_OVERHEAD_UHBR ? 32 : 8;
	bool is_mst = flags & DRM_DP_BW_OVERHEAD_MST;
	u32 overhead = 1000000;
	int symbol_cycles;
	/*
	 * DP Standard v2.1 2.6.4.1
	 * SSC downspread and ref clock variation margin:
	 *   5300ppm + 300ppm ~ 0.6%
	 */
	if (flags & DRM_DP_BW_OVERHEAD_SSC_REF_CLK)
		overhead += 6000;
	/*
	 * DP Standard v2.1 2.6.4.1.1, 3.5.1.5.4:
	 * FEC symbol insertions for 8b/10b channel coding:
	 * After each 250 data symbols on 2-4 lanes:
	 *   250 LL + 5 FEC_PARITY_PH + 1 CD_ADJ   (256 byte FEC block)
	 * After each 2 x 250 data symbols on 1 lane:
	 *   2 * 250 LL + 11 FEC_PARITY_PH + 1 CD_ADJ (512 byte FEC block)
	 * After 256 (2-4 lanes) or 128 (1 lane) FEC blocks:
	 *   256 * 256 bytes + 1 FEC_PM
	 * or
	 *   128 * 512 bytes + 1 FEC_PM
	 * (256 * 6 + 1) / (256 * 250) = 2.4015625 %
	 */
	if (flags & DRM_DP_BW_OVERHEAD_FEC)
		overhead += 24016;
	/*
	 * DP Standard v2.1 2.7.9, 5.9.7
	 * The FEC overhead for UHBR is accounted for in its 96.71% channel
	 * coding efficiency.
	 */
	WARN_ON((flags & DRM_DP_BW_OVERHEAD_UHBR) &&
		(flags & DRM_DP_BW_OVERHEAD_FEC));
	if (flags & DRM_DP_BW_OVERHEAD_DSC)
		symbol_cycles = drm_dp_link_dsc_symbol_cycles(lane_count, hactive,
							      dsc_slice_count,
							      bpp_x16, symbol_size,
							      is_mst);
	else
		symbol_cycles = drm_dp_link_symbol_cycles(lane_count, hactive,
							  bpp_x16, symbol_size,
							  is_mst);
	return DIV_ROUND_UP_ULL(mul_u32_u32(symbol_cycles * symbol_size * lane_count,
					    overhead * 16),
				hactive * bpp_x16);
}

/**
 * drm_dp_bw_channel_coding_efficiency - Get a DP link's channel coding efficiency
 * @is_uhbr: Whether the link has a 128b/132b channel coding
 *
 * Return the channel coding efficiency of the given DP link type, which is
 * either 8b/10b or 128b/132b (aka UHBR). The corresponding overhead includes
 * the 8b -> 10b, 128b -> 132b pixel data to link symbol conversion overhead
 * and for 128b/132b any link or PHY level control symbol insertion overhead
 * (LLCP, FEC, PHY sync, see DP Standard v2.1 3.5.2.18). For 8b/10b the
 * corresponding FEC overhead is BW allocation specific, included in the value
 * returned by drm_dp_bw_overhead().
 *
 * Returns the efficiency in the 100%/coding-overhead% ratio in
 * 1ppm units.
 */
int drm_dp_bw_channel_coding_efficiency(bool is_uhbr)
{
	if (is_uhbr)
		return 967100;
	else
		/*
		 * Note that on 8b/10b MST the efficiency is only
		 * 78.75% due to the 1 out of 64 MTPH packet overhead,
		 * not accounted for here.
		 */
		return 800000;
}
#endif
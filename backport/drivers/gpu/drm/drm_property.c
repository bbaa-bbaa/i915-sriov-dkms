#include <linux/version.h>
#include <linux/uaccess.h>

#include <drm/drm_crtc.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_print.h>
#include <drm/drm_property.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,8,0)
// https://github.com/gregkh/linux/commit/601603105325ad4ec62db95c9bc428202ece2c8f
/**
 * drm_property_replace_blob_from_id - replace a blob property taking a reference
 * @dev: DRM device
 * @blob: a pointer to the member blob to be replaced
 * @blob_id: the id of the new blob to replace with
 * @expected_size: expected size of the blob property
 * @expected_elem_size: expected size of an element in the blob property
 * @replaced: if the blob was in fact replaced
 *
 * Look up the new blob from id, take its reference, check expected sizes of
 * the blob and its element and replace the old blob by the new one. Advertise
 * if the replacement operation was successful.
 *
 * Return: true if the blob was in fact replaced. -EINVAL if the new blob was
 * not found or sizes don't match.
 */
int drm_property_replace_blob_from_id(struct drm_device *dev,
					 struct drm_property_blob **blob,
					 uint64_t blob_id,
					 ssize_t expected_size,
					 ssize_t expected_elem_size,
					 bool *replaced)
{
	struct drm_property_blob *new_blob = NULL;
	if (blob_id != 0) {
		new_blob = drm_property_lookup_blob(dev, blob_id);
		if (new_blob == NULL) {
			drm_dbg_atomic(dev,
				       "cannot find blob ID %llu\n", blob_id);
			return -EINVAL;
		}
		if (expected_size > 0 &&
		    new_blob->length != expected_size) {
			drm_dbg_atomic(dev,
				       "[BLOB:%d] length %zu different from expected %zu\n",
				       new_blob->base.id, new_blob->length, expected_size);
			drm_property_blob_put(new_blob);
			return -EINVAL;
		}
		if (expected_elem_size > 0 &&
		    new_blob->length % expected_elem_size != 0) {
			drm_dbg_atomic(dev,
				       "[BLOB:%d] length %zu not divisible by element size %zu\n",
				       new_blob->base.id, new_blob->length, expected_elem_size);
			drm_property_blob_put(new_blob);
			return -EINVAL;
		}
	}
	*replaced |= drm_property_replace_blob(blob, new_blob);
	drm_property_blob_put(new_blob);
	return 0;
}
#endif
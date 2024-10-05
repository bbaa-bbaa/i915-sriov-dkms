/*
 * Copyright © 2014 Red Hat
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/iopoll.h>
#include <linux/version.h>

#if IS_ENABLED(CONFIG_DRM_DEBUG_DP_MST_TOPOLOGY_REFS)
#include <linux/stacktrace.h>
#include <linux/sort.h>
#include <linux/timekeeping.h>
#include <linux/math64.h>
#endif

#include <drm/display/drm_dp_mst_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_fixed.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
// https://github.com/gregkh/linux/commit/9dcf67deeab6fbc4984175278b1b2c59881dca52

static bool drm_dp_mst_port_downstream_of_branch(struct drm_dp_mst_port *port,
						 struct drm_dp_mst_branch *branch)
{
	while (port->parent) {
		if (port->parent == branch)
			return true;

		if (port->parent->port_parent)
			port = port->parent->port_parent;
		else
			break;
	}
	return false;
}

static struct drm_dp_mst_port *
drm_dp_mst_topology_get_port_validated_locked(struct drm_dp_mst_branch *mstb,
					      struct drm_dp_mst_port *to_find)
{
	struct drm_dp_mst_port *port, *mport;

	list_for_each_entry(port, &mstb->ports, next) {
		if (port == to_find)
			return port;

		if (port->mstb) {
			mport = drm_dp_mst_topology_get_port_validated_locked(
			    port->mstb, to_find);
			if (mport)
				return mport;
		}
	}
	return NULL;
}

static bool
drm_dp_mst_port_downstream_of_parent_locked(struct drm_dp_mst_topology_mgr *mgr,
					    struct drm_dp_mst_port *port,
					    struct drm_dp_mst_port *parent)
{
	if (!mgr->mst_primary)
		return false;
	port = drm_dp_mst_topology_get_port_validated_locked(mgr->mst_primary,
							     port);
	if (!port)
		return false;
	if (!parent)
		return true;
	parent = drm_dp_mst_topology_get_port_validated_locked(mgr->mst_primary,
							       parent);
	if (!parent)
		return false;
	if (!parent->mstb)
		return false;
	return drm_dp_mst_port_downstream_of_branch(port, parent->mstb);
}
/**
 * drm_dp_mst_port_downstream_of_parent - check if a port is downstream of a parent port
 * @mgr: MST topology manager
 * @port: the port being looked up
 * @parent: the parent port
 *
 * The function returns %true if @port is downstream of @parent. If @parent is
 * %NULL - denoting the root port - the function returns %true if @port is in
 * @mgr's topology.
 */
bool
drm_dp_mst_port_downstream_of_parent(struct drm_dp_mst_topology_mgr *mgr,
				     struct drm_dp_mst_port *port,
				     struct drm_dp_mst_port *parent)
{
	bool ret;
	mutex_lock(&mgr->lock);
	ret = drm_dp_mst_port_downstream_of_parent_locked(mgr, port, parent);
	mutex_unlock(&mgr->lock);
	return ret;
}
#endif
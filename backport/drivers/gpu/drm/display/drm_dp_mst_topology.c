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

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
// https://github.com/gregkh/linux/commit/1cd0a5ea427931016c3e95b20dc20f17604937cc

static bool drm_dp_mst_is_end_device(u8 pdt, bool mcs)
{
	switch (pdt) {
	case DP_PEER_DEVICE_DP_LEGACY_CONV:
	case DP_PEER_DEVICE_SST_SINK:
		return true;
	case DP_PEER_DEVICE_MST_BRANCHING:
		/* For sst branch device */
		if (!mcs)
			return true;

		return false;
	}
	return true;
}


static int
drm_dp_mst_atomic_check_port_bw_limit(struct drm_dp_mst_port *port,
				      struct drm_dp_mst_topology_state *state,
				      struct drm_dp_mst_port **failing_port);

static int
drm_dp_mst_atomic_check_mstb_bw_limit(struct drm_dp_mst_branch *mstb,
				      struct drm_dp_mst_topology_state *state,
				      struct drm_dp_mst_port **failing_port)
{
	struct drm_dp_mst_atomic_payload *payload;
	struct drm_dp_mst_port *port;
	int pbn_used = 0, ret;
	bool found = false;

	/* Check that we have at least one port in our state that's downstream
	 * of this branch, otherwise we can skip this branch
	 */
	list_for_each_entry(payload, &state->payloads, next) {
		if (!payload->pbn ||
		    !drm_dp_mst_port_downstream_of_branch(payload->port, mstb))
			continue;

		found = true;
		break;
	}
	if (!found)
		return 0;

	if (mstb->port_parent)
		drm_dbg_atomic(mstb->mgr->dev,
			       "[MSTB:%p] [MST PORT:%p] Checking bandwidth limits on [MSTB:%p]\n",
			       mstb->port_parent->parent, mstb->port_parent, mstb);
	else
		drm_dbg_atomic(mstb->mgr->dev, "[MSTB:%p] Checking bandwidth limits\n", mstb);

	list_for_each_entry(port, &mstb->ports, next) {
		ret = drm_dp_mst_atomic_check_port_bw_limit(port, state, failing_port);
		if (ret < 0)
			return ret;

		pbn_used += ret;
	}

	return pbn_used;
}


static int
drm_dp_mst_atomic_check_port_bw_limit(struct drm_dp_mst_port *port,
				      struct drm_dp_mst_topology_state *state,
				      struct drm_dp_mst_port **failing_port)
{
	struct drm_dp_mst_atomic_payload *payload;
	int pbn_used = 0;

	if (port->pdt == DP_PEER_DEVICE_NONE)
		return 0;

	if (drm_dp_mst_is_end_device(port->pdt, port->mcs)) {
		payload = drm_atomic_get_mst_payload_state(state, port);
		if (!payload)
			return 0;

		/*
		 * This could happen if the sink deasserted its HPD line, but
		 * the branch device still reports it as attached (PDT != NONE).
		 */
		if (!port->full_pbn) {
			drm_dbg_atomic(port->mgr->dev,
				       "[MSTB:%p] [MST PORT:%p] no BW available for the port\n",
				       port->parent, port);
			*failing_port = port;
			return -EINVAL;
		}

		pbn_used = payload->pbn;
	} else {
		pbn_used = drm_dp_mst_atomic_check_mstb_bw_limit(port->mstb,
								 state,
								 failing_port);
		if (pbn_used <= 0)
			return pbn_used;
	}

	if (pbn_used > port->full_pbn) {
		drm_dbg_atomic(port->mgr->dev,
			       "[MSTB:%p] [MST PORT:%p] required PBN of %d exceeds port limit of %d\n",
			       port->parent, port, pbn_used, port->full_pbn);
		*failing_port = port;
		return -ENOSPC;
	}

	drm_dbg_atomic(port->mgr->dev, "[MSTB:%p] [MST PORT:%p] uses %d out of %d PBN\n",
		       port->parent, port, pbn_used, port->full_pbn);

	return pbn_used;
}


static inline int
drm_dp_mst_atomic_check_payload_alloc_limits(struct drm_dp_mst_topology_mgr *mgr,
					     struct drm_dp_mst_topology_state *mst_state)
{
	struct drm_dp_mst_atomic_payload *payload;
	int avail_slots = mst_state->total_avail_slots, payload_count = 0;

	list_for_each_entry(payload, &mst_state->payloads, next) {
		/* Releasing payloads is always OK-even if the port is gone */
		if (payload->delete) {
			drm_dbg_atomic(mgr->dev, "[MST PORT:%p] releases all time slots\n",
				       payload->port);
			continue;
		}

		drm_dbg_atomic(mgr->dev, "[MST PORT:%p] requires %d time slots\n",
			       payload->port, payload->time_slots);

		avail_slots -= payload->time_slots;
		if (avail_slots < 0) {
			drm_dbg_atomic(mgr->dev,
				       "[MST PORT:%p] not enough time slots in mst state %p (avail=%d)\n",
				       payload->port, mst_state, avail_slots + payload->time_slots);
			return -ENOSPC;
		}

		if (++payload_count > mgr->max_payloads) {
			drm_dbg_atomic(mgr->dev,
				       "[MST MGR:%p] state %p has too many payloads (max=%d)\n",
				       mgr, mst_state, mgr->max_payloads);
			return -EINVAL;
		}

		/* Assign a VCPI */
		if (!payload->vcpi) {
			payload->vcpi = ffz(mst_state->payload_mask) + 1;
			drm_dbg_atomic(mgr->dev, "[MST PORT:%p] assigned VCPI #%d\n",
				       payload->port, payload->vcpi);
			mst_state->payload_mask |= BIT(payload->vcpi - 1);
		}
	}

	if (!payload_count)
		mst_state->pbn_div = 0;

	drm_dbg_atomic(mgr->dev, "[MST MGR:%p] mst state %p TU pbn_div=%d avail=%d used=%d\n",
		       mgr, mst_state, mst_state->pbn_div, avail_slots,
		       mst_state->total_avail_slots - avail_slots);

	return 0;
}

/**
 * drm_dp_mst_atomic_check_mgr - Check the atomic state of an MST topology manager
 * @state: The global atomic state
 * @mgr: Manager to check
 * @mst_state: The MST atomic state for @mgr
 * @failing_port: Returns the port with a BW limitation
 *
 * Checks the given MST manager's topology state for an atomic update to ensure
 * that it's valid. This includes checking whether there's enough bandwidth to
 * support the new timeslot allocations in the atomic update.
 *
 * Any atomic drivers supporting DP MST must make sure to call this or
 * the drm_dp_mst_atomic_check() function after checking the rest of their state
 * in their &drm_mode_config_funcs.atomic_check() callback.
 *
 * See also:
 * drm_dp_mst_atomic_check()
 * drm_dp_atomic_find_time_slots()
 * drm_dp_atomic_release_time_slots()
 *
 * Returns:
 *   - 0 if the new state is valid
 *   - %-ENOSPC, if the new state is invalid, because of BW limitation
 *         @failing_port is set to:
 *
 *         - The non-root port where a BW limit check failed
 *           with all the ports downstream of @failing_port passing
 *           the BW limit check.
 *           The returned port pointer is valid until at least
 *           one payload downstream of it exists.
 *         - %NULL if the BW limit check failed at the root port
 *           with all the ports downstream of the root port passing
 *           the BW limit check.
 *
 *   - %-EINVAL, if the new state is invalid, because the root port has
 *     too many payloads.
 */
int drm_dp_mst_atomic_check_mgr(struct drm_atomic_state *state,
				struct drm_dp_mst_topology_mgr *mgr,
				struct drm_dp_mst_topology_state *mst_state,
				struct drm_dp_mst_port **failing_port)
{
	int ret;

	*failing_port = NULL;

	if (!mgr->mst_state)
		return 0;

	mutex_lock(&mgr->lock);
	ret = drm_dp_mst_atomic_check_mstb_bw_limit(mgr->mst_primary,
						    mst_state,
						    failing_port);
	mutex_unlock(&mgr->lock);

	if (ret < 0)
		return ret;

	return drm_dp_mst_atomic_check_payload_alloc_limits(mgr, mst_state);
}
#endif
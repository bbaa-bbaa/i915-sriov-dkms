// SPDX-License-Identifier: GPL-2.0
/*
 * DisplayPort CEC-Tunneling-over-AUX support
 *
 * Copyright 2018 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <media/cec.h>

#include <drm/display/drm_dp_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_device.h>
#include <drm/drm_edid.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,7,0)
// https://github.com/gregkh/linux/commit/113cdddcded6d597b64d824a59d0186db150113a
/*
 * A new EDID is set. If there is no CEC adapter, then create one. If
 * there was a CEC adapter, then check if the CEC adapter properties
 * were unchanged and just update the CEC physical address. Otherwise
 * unregister the old CEC adapter and create a new one.
 */

static bool drm_dp_cec_cap(struct drm_dp_aux *aux, u8 *cec_cap)
{
	u8 cap = 0;

	if (drm_dp_dpcd_readb(aux, DP_CEC_TUNNELING_CAPABILITY, &cap) != 1 ||
	    !(cap & DP_CEC_TUNNELING_CAPABLE))
		return false;
	if (cec_cap)
		*cec_cap = cap;
	return true;
}

static int drm_dp_cec_adap_enable(struct cec_adapter *adap, bool enable)
{
	struct drm_dp_aux *aux = cec_get_drvdata(adap);
	u32 val = enable ? DP_CEC_TUNNELING_ENABLE : 0;
	ssize_t err = 0;

	err = drm_dp_dpcd_writeb(aux, DP_CEC_TUNNELING_CONTROL, val);
	return (enable && err < 0) ? err : 0;
}

static int drm_dp_cec_adap_log_addr(struct cec_adapter *adap, u8 addr)
{
	struct drm_dp_aux *aux = cec_get_drvdata(adap);
	/* Bit 15 (logical address 15) should always be set */
	u16 la_mask = 1 << CEC_LOG_ADDR_BROADCAST;
	u8 mask[2];
	ssize_t err;

	if (addr != CEC_LOG_ADDR_INVALID)
		la_mask |= adap->log_addrs.log_addr_mask | (1 << addr);
	mask[0] = la_mask & 0xff;
	mask[1] = la_mask >> 8;
	err = drm_dp_dpcd_write(aux, DP_CEC_LOGICAL_ADDRESS_MASK, mask, 2);
	return (addr != CEC_LOG_ADDR_INVALID && err < 0) ? err : 0;
}

static int drm_dp_cec_adap_transmit(struct cec_adapter *adap, u8 attempts,
				    u32 signal_free_time, struct cec_msg *msg)
{
	struct drm_dp_aux *aux = cec_get_drvdata(adap);
	unsigned int retries = min(5, attempts - 1);
	ssize_t err;

	err = drm_dp_dpcd_write(aux, DP_CEC_TX_MESSAGE_BUFFER,
				msg->msg, msg->len);
	if (err < 0)
		return err;

	err = drm_dp_dpcd_writeb(aux, DP_CEC_TX_MESSAGE_INFO,
				 (msg->len - 1) | (retries << 4) |
				 DP_CEC_TX_MESSAGE_SEND);
	return err < 0 ? err : 0;
}

static int drm_dp_cec_adap_monitor_all_enable(struct cec_adapter *adap,
					      bool enable)
{
	struct drm_dp_aux *aux = cec_get_drvdata(adap);
	ssize_t err;
	u8 val;

	if (!(adap->capabilities & CEC_CAP_MONITOR_ALL))
		return 0;

	err = drm_dp_dpcd_readb(aux, DP_CEC_TUNNELING_CONTROL, &val);
	if (err >= 0) {
		if (enable)
			val |= DP_CEC_SNOOPING_ENABLE;
		else
			val &= ~DP_CEC_SNOOPING_ENABLE;
		err = drm_dp_dpcd_writeb(aux, DP_CEC_TUNNELING_CONTROL, val);
	}
	return (enable && err < 0) ? err : 0;
}

static void drm_dp_cec_adap_status(struct cec_adapter *adap,
				   struct seq_file *file)
{
	struct drm_dp_aux *aux = cec_get_drvdata(adap);
	struct drm_dp_desc desc;
	struct drm_dp_dpcd_ident *id = &desc.ident;

	if (drm_dp_read_desc(aux, &desc, true))
		return;
    seq_printf(file, "OUI: %*phD\n",
    (int)sizeof(id->oui), id->oui);
	seq_printf(file, "ID: %*pE\n",
		   (int)strnlen(id->device_id, sizeof(id->device_id)),
		   id->device_id);
	seq_printf(file, "HW Rev: %d.%d\n", id->hw_rev >> 4, id->hw_rev & 0xf);
	/*
	 * Show this both in decimal and hex: at least one vendor
	 * always reports this in hex.
	 */
	seq_printf(file, "FW/SW Rev: %d.%d (0x%02x.0x%02x)\n",
		   id->sw_major_rev, id->sw_minor_rev,
		   id->sw_major_rev, id->sw_minor_rev);
}

static const struct cec_adap_ops drm_dp_cec_adap_ops = {
	.adap_enable = drm_dp_cec_adap_enable,
	.adap_log_addr = drm_dp_cec_adap_log_addr,
	.adap_transmit = drm_dp_cec_adap_transmit,
	.adap_monitor_all_enable = drm_dp_cec_adap_monitor_all_enable,
	.adap_status = drm_dp_cec_adap_status,
};

void drm_dp_cec_attach(struct drm_dp_aux *aux, u16 source_physical_address)
{
	struct drm_connector *connector = aux->cec.connector;
	u32 cec_caps = CEC_CAP_DEFAULTS | CEC_CAP_NEEDS_HPD |
		       CEC_CAP_CONNECTOR_INFO;
	struct cec_connector_info conn_info;
	unsigned int num_las = 1;
	u8 cap;

	/* No transfer function was set, so not a DP connector */
	if (!aux->transfer)
		return;

#ifndef CONFIG_MEDIA_CEC_RC
	/*
	 * CEC_CAP_RC is part of CEC_CAP_DEFAULTS, but it is stripped by
	 * cec_allocate_adapter() if CONFIG_MEDIA_CEC_RC is undefined.
	 *
	 * Do this here as well to ensure the tests against cec_caps are
	 * correct.
	 */
	cec_caps &= ~CEC_CAP_RC;
#endif
	cancel_delayed_work_sync(&aux->cec.unregister_work);

	mutex_lock(&aux->cec.lock);
	if (!drm_dp_cec_cap(aux, &cap)) {
		/* CEC is not supported, unregister any existing adapter */
		cec_unregister_adapter(aux->cec.adap);
		aux->cec.adap = NULL;
		goto unlock;
	}

	if (cap & DP_CEC_SNOOPING_CAPABLE)
		cec_caps |= CEC_CAP_MONITOR_ALL;
	if (cap & DP_CEC_MULTIPLE_LA_CAPABLE)
		num_las = CEC_MAX_LOG_ADDRS;

	if (aux->cec.adap) {
		if (aux->cec.adap->capabilities == cec_caps &&
		    aux->cec.adap->available_log_addrs == num_las) {
			/* Unchanged, so just set the phys addr */
			cec_s_phys_addr(aux->cec.adap, source_physical_address, false);
			goto unlock;
		}
		/*
		 * The capabilities changed, so unregister the old
		 * adapter first.
		 */
		cec_unregister_adapter(aux->cec.adap);
	}

	/* Create a new adapter */
	aux->cec.adap = cec_allocate_adapter(&drm_dp_cec_adap_ops,
					     aux, connector->name, cec_caps,
					     num_las);
	if (IS_ERR(aux->cec.adap)) {
		aux->cec.adap = NULL;
		goto unlock;
	}

	cec_fill_conn_info_from_drm(&conn_info, connector);
	cec_s_conn_info(aux->cec.adap, &conn_info);

	if (cec_register_adapter(aux->cec.adap, connector->dev->dev)) {
		cec_delete_adapter(aux->cec.adap);
		aux->cec.adap = NULL;
	} else {
		/*
		 * Update the phys addr for the new CEC adapter. When called
		 * from drm_dp_cec_register_connector() edid == NULL, so in
		 * that case the phys addr is just invalidated.
		 */
		cec_s_phys_addr(aux->cec.adap, source_physical_address, false);
	}
unlock:
	mutex_unlock(&aux->cec.lock);
}


/*
 * Note: Prefer calling drm_dp_cec_attach() with
 * connector->display_info.source_physical_address if possible.
 */
void drm_dp_cec_set_edid(struct drm_dp_aux *aux, const struct edid *edid)
{
	u16 pa = CEC_PHYS_ADDR_INVALID;

	if (edid && edid->extensions)
		pa = cec_get_edid_phys_addr((const u8 *)edid,
					    EDID_LENGTH * (edid->extensions + 1), NULL);

	drm_dp_cec_attach(aux, pa);
}

#endif
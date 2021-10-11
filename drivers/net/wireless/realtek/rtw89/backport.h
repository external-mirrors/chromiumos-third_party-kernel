/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2024 Realtek Corporation
 */

#ifndef __RTW89_BACKPORT_H__
#define __RTW89_BACKPORT_H__

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 13, 0))

#define IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU \
	IEEE80211_HE_MAC_CAP4_AMDSU_IN_AMPDU

#define IEEE80211_HE_PHY_CAP3_RX_PARTIAL_BW_SU_IN_20MHZ_MU \
	IEEE80211_HE_PHY_CAP3_RX_HE_MU_PPDU_FROM_NON_AP_STA

#define IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMING_FB \
	IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMER_FB

#define IEEE80211_HE_PHY_CAP7_POWER_BOOST_FACTOR_SUPP \
	IEEE80211_HE_PHY_CAP7_POWER_BOOST_FACTOR_AR

#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_2 \
	IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_VHT_2

#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(5, 13, 0) */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))

static inline
int request_partial_firmware_into_buf(const struct firmware **firmware_p,
				      const char *name, struct device *device,
				      void *buf, size_t size, size_t offset)
{
	int ret = request_firmware(firmware_p, name, device);

	if (ret)
		return ret;

	memcpy(buf, (*firmware_p)->data + offset, size);
	return 0;
}

#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0) */

#endif

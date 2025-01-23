/*
 * ChromeOS backport definitions
 * Copyright (C) 2015-2017 Intel Deutschland GmbH
 * Copyright (C) 2018-2024 Intel Corporation
 */

/* backport wiphy_ext_feature_set/_isset
 *
 * To do so, define our own versions thereof that check for a negative
 * feature index and in that case ignore it entirely. That allows us to
 * define the ones that the cfg80211 version doesn't support to -1.
 */
static inline void iwl7000_wiphy_ext_feature_set(struct wiphy *wiphy, int ftidx)
{
	if (ftidx < 0)
		return;
	wiphy_ext_feature_set(wiphy, ftidx);
}

static inline bool iwl7000_wiphy_ext_feature_isset(struct wiphy *wiphy,
						   int ftidx)
{
	if (ftidx < 0)
		return false;
	return wiphy_ext_feature_isset(wiphy, ftidx);
}
#define wiphy_ext_feature_set iwl7000_wiphy_ext_feature_set
#define wiphy_ext_feature_isset iwl7000_wiphy_ext_feature_isset

static inline int pcim_request_all_regions(struct pci_dev *pdev, const char *name)
{
	/* NOTE: this only works with pcim_enable_device() on older kernels */
	int mask = 0;

	for (int i = 0; i < PCI_STD_NUM_BARS; i++) {
		if (!pci_resource_start(pdev, i))
			continue;
		if (!pci_resource_len(pdev, i))
			continue;
		mask |= BIT(i);
	}

	return pci_request_selected_regions(pdev, mask, name);
}


struct cfg80211_mlo_reconf_done_data {
	const u8 *buf;
	size_t len;
	u16 added_links;
	struct {
		struct cfg80211_bss *bss;
		u8 *addr;
	} links[IEEE80211_MLD_MAX_NUM_LINKS];
};

struct cfg80211_ml_reconf_req {
	struct cfg80211_assoc_link add_links[IEEE80211_MLD_MAX_NUM_LINKS];
	u16 rem_links;
};

static inline void
cfg80211_mlo_reconf_add_done(struct net_device *dev,
			     struct cfg80211_mlo_reconf_done_data *data)
{
}

static inline void
cfg80211_epcs_changed(struct net_device *netdev, bool enabled)
{
}

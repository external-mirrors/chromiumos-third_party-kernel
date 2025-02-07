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


static inline void
cfg80211_epcs_changed(struct net_device *netdev, bool enabled)
{
}

DEFINE_GUARD(wiphy, struct wiphy *,
        mutex_lock(&_T->mtx),
        mutex_unlock(&_T->mtx))

static inline int __printf(2, 3) debugfs_change_name(struct dentry *dentry, const char *fmt, ...)
{
	const char *new_name;
	struct dentry *parent;
	va_list ap;

	va_start(ap, fmt);
	new_name = kvasprintf_const(GFP_KERNEL, fmt, ap);
	va_end(ap);
	if (!new_name)
		return -ENOMEM;

	parent = dget_parent(dentry);

	debugfs_rename(parent, dentry, parent, new_name);

	dput(parent);
	kfree_const(new_name);
	/* We never checked the succession of debugfs_rename anyway */
	return 0;
}

#define NL80211_RRF_ALLOW_20MHZ_ACTIVITY    BIT(25)

static inline int cfg80211_chandef_get_width(const struct cfg80211_chan_def *c)
{
	return nl80211_chan_width_to_mhz(c->width);
}

/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2024 Realtek Corporation
 */

#ifndef __RTW89_BACKPORT_H__
#define __RTW89_BACKPORT_H__

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 5, 0))
#define __counted_by(m)
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(6, 5, 0) */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0))
#define ieee80211_nullfunc_get(hw, vif, link_id, qos_ok) \
	ieee80211_nullfunc_get(hw, vif, qos_ok)
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0) */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 18, 0))
static inline const char *str_enable_disable(bool v)
{
	return v ? "enable" : "disable";
}
#define str_disable_enable(v)		str_enable_disable(!(v))
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(5, 18, 0)) */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 16, 0))
#ifdef __cplusplus
/* sizeof(struct{}) is 1 in C++, not 0, can't use C version of the macro. */
#define __DECLARE_FLEX_ARRAY(T, member)	\
	T member[0]
#else
/**
 * __DECLARE_FLEX_ARRAY() - Declare a flexible array usable in a union
 *
 * @TYPE: The type of each flexible array element
 * @NAME: The name of the flexible array member
 *
 * In order to have a flexible array member in a union or alone in a
 * struct, it needs to be wrapped in an anonymous struct with at least 1
 * named member, but that member can be empty.
 */
#define __DECLARE_FLEX_ARRAY(TYPE, NAME)	\
	struct { \
		struct { } __empty_ ## NAME; \
		TYPE NAME[]; \
	}
#endif

/**
 * DECLARE_FLEX_ARRAY() - Declare a flexible array usable in a union
 *
 * @TYPE: The type of each flexible array element
 * @NAME: The name of the flexible array member
 *
 * In order to have a flexible array member in a union or alone in a
 * struct, it needs to be wrapped in an anonymous struct with at least 1
 * named member, but that member can be empty.
 */
#define DECLARE_FLEX_ARRAY(TYPE, NAME) \
	__DECLARE_FLEX_ARRAY(TYPE, NAME)
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(5, 16, 0) */

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

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 8, 0))
/**
 * size_mul() - Calculate size_t multiplication with saturation at SIZE_MAX
 * @factor1: first factor
 * @factor2: second factor
 *
 * Returns: calculate @factor1 * @factor2, both promoted to size_t,
 * with any overflow causing the return value to be SIZE_MAX. The
 * lvalue must be size_t to avoid implicit type conversion.
 */
static inline size_t __must_check size_mul(size_t factor1, size_t factor2)
{
	size_t bytes;

	if (check_mul_overflow(factor1, factor2, &bytes))
		return SIZE_MAX;

	return bytes;
}

/**
 * flex_array_size() - Calculate size of a flexible array member
 *                     within an enclosing structure.
 * @p: Pointer to the structure.
 * @member: Name of the flexible array member.
 * @count: Number of elements in the array.
 *
 * Calculates size of a flexible array of @count number of @member
 * elements, at the end of structure @p.
 *
 * Return: number of bytes needed or SIZE_MAX on overflow.
 */
#define flex_array_size(p, member, count)				\
	__builtin_choose_expr(__is_constexpr(count),			\
		(count) * sizeof(*(p)->member) + __must_be_array((p)->member),	\
		size_mul(count, sizeof(*(p)->member) + __must_be_array((p)->member)))
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(5, 8, 0) */

#endif

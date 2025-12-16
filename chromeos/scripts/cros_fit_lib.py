# SPDX-License-Identifier: GPL-2.0+ OR Apache-2.0 OR BSD-3-Clause

"""Library for building ChromeOS FIT."""

import collections
import copy
import dataclasses
import json
import os
from typing import List

import yaml


DTB_CONFIG_DEFAULT_KEY = "DEFAULT"
DTB_CONFIG_KEY_SKUS = "skus"
DTB_CONFIG_KEY_DTB = "dtb"
DTB_CONFIG_KEY_DTBO = "dtbo"


@dataclasses.dataclass
class SkuConfig:
    """Class of SKU config."""

    model: str
    sku: int
    fw_config: int


@dataclasses.dataclass
class FitFdtNode:
    """Class of FIT FDT/FDTO node."""

    name: str
    filename: str


@dataclasses.dataclass
class FitConfigNode:
    """Class of FIT configuration node."""

    description: str
    compatible: bytes
    fdt: List[str]


def _read_dtb_config(dtb_config_file):
    with open(dtb_config_file, "r", encoding="utf-8") as f:
        dtb_configs = yaml.safe_load(f)
    model_sku_configs = collections.defaultdict(list)
    model_dtb_configs = {}
    default_config = dtb_configs.get(DTB_CONFIG_DEFAULT_KEY, {})
    for model, config in dtb_configs.items():
        if model == DTB_CONFIG_DEFAULT_KEY:
            continue
        # skus
        skus = set()
        for sku_config in config.get(DTB_CONFIG_KEY_SKUS, []):
            sku = sku_config["sku"]
            fw_config = sku_config["fw_config"]
            if sku in skus:
                raise ValueError(f"Duplicate sku {sku} for {model}")
            skus.add(sku)
            model_sku_configs[model].append(SkuConfig(model, sku, fw_config))
        # dtb/dtbo
        dtbs = config[DTB_CONFIG_KEY_DTB]
        dtbos = copy.deepcopy(default_config.get(DTB_CONFIG_KEY_DTBO, {}))
        dtbos.update(config.get(DTB_CONFIG_KEY_DTBO, {}))
        model_dtb_configs[model] = (dtbs, dtbos)
    return model_sku_configs, model_dtb_configs


def _get_chromeos_skus(chromeos_config_file):
    with open(chromeos_config_file, "r", encoding="utf-8") as f:
        chromeos_configs = json.load(f)
    model_skus = set()
    model_sku_configs = collections.defaultdict(list)
    for config in chromeos_configs["chromeos"]["configs"]:
        # Get model name (i.e. coreboot MAINBOARD_PART_NUMBER) from FRID.
        frid = config["identity"]["frid"]
        if not frid.startswith("Google_"):
            raise ValueError(f'Wrong frid format for {config["name"]}: {frid}')
        model = frid.removeprefix("Google_").lower()
        sku = config["identity"]["sku-id"]
        fw = config.get("firmware")
        if not fw:
            print(f"Missing firmware for {model} SKU {sku}; skipping")
            continue
        fw_config = fw["firmware-config"]
        if (model, sku) in model_skus:
            raise ValueError(f"Duplicate sku {sku} for {model}")
        model_sku_configs[model].append(SkuConfig(model, sku, fw_config))
    return model_sku_configs


def _get_revisions(dtb_config):
    """Get a list of revisions and a representative of latest revision."""
    rev_min_min = None
    rev_min_max = None
    revs = set()
    rev_latest = 0
    dtbs, dtbos = dtb_config
    all_dtbs = {**dtbs, **dtbos}
    for dtb, attr in all_dtbs.items():
        rev_attr = attr.get("rev")
        if not rev_attr:
            continue
        rev_min = rev_attr.get("min", 0)
        rev_max = rev_attr.get("max")
        if rev_min < 0:
            raise ValueError(f"Negative rev min in {dtb}: {rev_min}")
        if rev_max is not None:
            # If the range is [a, b] (inclusive), then all revisions in the
            # range are added to the list. The representative of the latest
            # revision is set as b+1.
            revs |= set(range(rev_min, rev_max + 1))
            rev_latest = max(rev_latest, rev_max + 1)
        else:
            # If the range is [c, INF) and there is another range [c0, INF) with
            # c0 < c, then revisions in [c0, c-1] should be added to the list.
            # This ensures [c0, c-1] are distinguishable from [c, INF).
            # Here we keep track of rev_min_min/rev_min_max. The rev list will
            # be updated outside the loop.
            if rev_min_min is None or rev_min < rev_min_min:
                rev_min_min = rev_min
            if rev_min_max is None or rev_min > rev_min_max:
                rev_min_max = rev_min
            rev_latest = max(rev_latest, rev_min)

    if rev_min_min is not None and rev_min_max is not None:
        revs |= set(range(rev_min_min, rev_min_max))

    revs.add(rev_latest)
    return sorted(revs), rev_latest


def _match_dtb(dtb_attr, rev, fw_config):
    """Match a DTB/DTBO.

    Args:
        dtb_attr: Matching attributes for the DTB.
        rev: Board revision.
        fw_config: Firmware configuration.

    Returns:
        Whether the DTB is matched or not.
    """
    rev_attr = dtb_attr.get("rev")
    if rev_attr:
        rev_min = rev_attr.get("min")
        rev_max = rev_attr.get("max")
        if rev_min is not None and rev < rev_min:
            return False
        if rev_max is not None and rev > rev_max:
            return False
    fw_config_attr = dtb_attr.get("fw_config")
    if fw_config_attr:
        mask_attr = fw_config_attr["mask"]
        value_attr = fw_config_attr["value"]
        if (fw_config & mask_attr) != value_attr:
            return False
    return True


def _gen_fit_dtb_nodes(sku_dtb_configs):
    fit_dtb_nodes = []
    dtb_nodes = {}
    for rev_sku_configs in sku_dtb_configs.values():
        for dtb, _ in rev_sku_configs.values():
            if dtb in dtb_nodes:
                continue
            if os.path.splitext(dtb)[1] != ".dtb":
                raise ValueError(f"Wrong file extension for dtb file: {dtb}")
            seq = len(dtb_nodes) + 1
            node_name = f"fdt-{seq}"
            dtb_nodes[dtb] = node_name
            fit_dtb_nodes.append(FitFdtNode(node_name, dtb))
    dtbo_nodes = {}
    for rev_sku_configs in sku_dtb_configs.values():
        for _, dtbos in rev_sku_configs.values():
            for dtbo in dtbos:
                if dtbo in dtbo_nodes:
                    continue
                if os.path.splitext(dtbo)[1] != ".dtbo":
                    raise ValueError(
                        f"Wrong file extension for dtbo file: {dtbo}"
                    )
                seq = len(dtbo_nodes) + 1
                node_name = f"fdto-{seq}"
                dtbo_nodes[dtbo] = node_name
                fit_dtb_nodes.append(FitFdtNode(node_name, dtbo))
    return dtb_nodes, dtbo_nodes, fit_dtb_nodes


def _gen_fit_config_nodes(sku_dtb_configs, dtb_nodes, dtbo_nodes):
    # Merge <rev, sku> pairs with same DTB & DTBOs.
    config_nodes = []
    for model, rev_sku_configs in sku_dtb_configs.items():
        if not rev_sku_configs:
            continue

        rev_latest = max(rev for rev, _ in rev_sku_configs)
        dtb_key_to_sku = {}
        equiv_skus = collections.defaultdict(list)
        for rev_sku in sorted(rev_sku_configs):
            dtb, dtbos = rev_sku_configs[rev_sku]
            dtb_key = tuple([dtb] + dtbos)
            equiv_sku = dtb_key_to_sku.get(dtb_key)
            if equiv_sku is not None:
                equiv_skus[equiv_sku].append(rev_sku)
            else:
                equiv_skus[rev_sku].append(rev_sku)
                dtb_key_to_sku[dtb_key] = rev_sku
        for equiv_sku, rev_skus in equiv_skus.items():
            dtb, dtbos = rev_sku_configs[equiv_sku]
            rev_sku_dict = collections.defaultdict(list)
            for rev, sku in rev_skus:
                rev_sku_dict[rev].append(sku)
            desc_list = [f"Google {model.title()}"]
            compat_list = []
            compat_prefix = f"google,{model}"
            for rev, skus in rev_sku_dict.items():
                if rev != rev_latest:
                    desc_list.append(f"rev {rev}")
                skus_desc = "/".join(str(sku) for sku in sorted(skus))
                desc_list.append(f"sku {skus_desc}")
                rev_compat = f"-rev{rev}" if rev != rev_latest else ""
                for sku in sorted(skus):
                    compat_list.append(f"{compat_prefix}{rev_compat}-sku{sku}")
            description = " ".join(desc_list)
            compat = bytes("".join(f"{x}\x00" for x in compat_list), "ascii")
            fdt_nodes = [dtb_nodes[dtb]]
            fdt_nodes += [dtbo_nodes[dtbo] for dtbo in dtbos]
            config_nodes.append(FitConfigNode(description, compat, fdt_nodes))
    return config_nodes


def process_dtb_config(dtb_config_file: str, chromeos_config_file: str):
    """Process DTB config file based on ChromeOS config file.

    Args:
        dtb_config_file: Kernel DTB/DTBO config file.
        chromeos_config_file: ChromeOS config file.

    Returns:
        (fdt_nodes, config_nodes):
            fdt_nodes: List of FitFdtNode.
            config_nodes: List of FitConfigNode.
    """
    model_sku_configs, model_dtb_configs = _read_dtb_config(dtb_config_file)
    if chromeos_config_file:
        model_sku_configs = _get_chromeos_skus(chromeos_config_file)

    # Generate configs per <rev, sku> pair.
    sku_dtb_configs = collections.defaultdict(dict)
    for model, sku_configs in model_sku_configs.items():
        # Skip models not in dtb_config_file.
        dtb_config = model_dtb_configs.get(model)
        if not dtb_config:
            continue

        rev_list, rev_latest = _get_revisions(dtb_config)

        for rev in rev_list:
            for sku_config in sku_configs:
                sku = sku_config.sku
                fw_config = sku_config.fw_config
                model_sku_str = (
                    f"{model} (sku {sku})"
                    if rev == rev_latest
                    else f"{model} (rev {rev}, sku {sku})"
                )

                dtbs, dtbos = dtb_config
                matched_dtbs = []
                for dtb, attr in dtbs.items():
                    if _match_dtb(attr, rev, fw_config):
                        matched_dtbs.append(dtb)
                if not matched_dtbs:
                    raise ValueError(
                        f"Unable to match a dtb for {model_sku_str}, "
                        f"fw_config 0x{fw_config:08x}"
                    )
                if len(matched_dtbs) > 1:
                    raise ValueError(
                        f"Multiple matched dtbs for {model_sku_str}: "
                        f"{matched_dtbs}"
                    )
                matched_dtb = matched_dtbs[0]
                matched_dtbos = []
                for dtbo, attr in dtbos.items():
                    if _match_dtb(attr, rev, fw_config):
                        matched_dtbos.append(dtbo)
                sku_dtb_configs[model][(rev, sku)] = (
                    matched_dtb,
                    matched_dtbos,
                )

    dtb_nodes, dtbo_nodes, fit_dtb_nodes = _gen_fit_dtb_nodes(sku_dtb_configs)
    fit_config_nodes = _gen_fit_config_nodes(
        sku_dtb_configs, dtb_nodes, dtbo_nodes
    )

    return fit_dtb_nodes, fit_config_nodes

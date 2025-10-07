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
    sku_configs = []
    model_dtb_configs = {}
    default_config = dtb_configs.get(DTB_CONFIG_DEFAULT_KEY, {})
    for model, config in dtb_configs.items():
        if model == DTB_CONFIG_DEFAULT_KEY:
            continue
        # skus
        for sku_config in config.get(DTB_CONFIG_KEY_SKUS, []):
            sku = sku_config["sku"]
            fw_config = sku_config["fw_config"]
            sku_configs.append(SkuConfig(model, sku, fw_config))
        # dtb/dtbo
        dtbs = config[DTB_CONFIG_KEY_DTB]
        dtbos = copy.deepcopy(default_config.get(DTB_CONFIG_KEY_DTBO, {}))
        dtbos.update(config.get(DTB_CONFIG_KEY_DTBO, {}))
        model_dtb_configs[model] = (dtbs, dtbos)
    return sku_configs, model_dtb_configs


def _get_chromeos_skus(chromeos_config_file):
    with open(chromeos_config_file, "r", encoding="utf-8") as f:
        chromeos_configs = json.load(f)
    sku_configs = []
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
        sku_configs.append(SkuConfig(model, sku, fw_config))
    return sku_configs


def _match_dtb(dtb_attr, sku, fw_config):
    sku_attr = dtb_attr.get("sku")
    if sku_attr and sku not in sku_attr:
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
    for sku_configs in sku_dtb_configs.values():
        for dtb, _ in sku_configs.values():
            if dtb in dtb_nodes:
                continue
            if os.path.splitext(dtb)[1] != ".dtb":
                raise ValueError(f"Wrong file extension for dtb file: {dtb}")
            seq = len(dtb_nodes) + 1
            node_name = f"fdt-{seq}"
            dtb_nodes[dtb] = node_name
            fit_dtb_nodes.append(FitFdtNode(node_name, dtb))
    dtbo_nodes = {}
    for sku_configs in sku_dtb_configs.values():
        for _, dtbos in sku_configs.values():
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
    # Merge SKUs with same DTB & DTBOs.
    config_nodes = []
    for model, sku_configs in sku_dtb_configs.items():
        dtb_key_to_sku = {}
        equiv_skus = collections.defaultdict(list)
        for sku in sorted(sku_configs):
            dtb, dtbos = sku_configs[sku]
            dtb_key = tuple([dtb] + dtbos)
            equiv_sku = dtb_key_to_sku.get(dtb_key)
            if equiv_sku is not None:
                equiv_skus[equiv_sku].append(sku)
            else:
                equiv_skus[sku].append(sku)
                dtb_key_to_sku[dtb_key] = sku
        for equiv_sku, skus in equiv_skus.items():
            dtb, dtbos = sku_configs[equiv_sku]
            skus_str = "/".join(str(sku) for sku in skus)
            description = f"Google {model.title()} SKU {skus_str}"
            if len(equiv_skus) == 1:
                compat_list = [f"google,{model}"]
            else:
                compat_list = [f"google,{model}-sku{sku}" for sku in skus]
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
    sku_configs, model_dtb_configs = _read_dtb_config(dtb_config_file)
    if chromeos_config_file:
        sku_configs = _get_chromeos_skus(chromeos_config_file)

    # Generate per-SKU configs.
    sku_dtb_configs = collections.defaultdict(dict)
    for sku_config in sku_configs:
        model = sku_config.model
        sku = sku_config.sku
        fw_config = sku_config.fw_config
        # Skip models not in dtb_config_file.
        dtb_config = model_dtb_configs.get(model)
        if not dtb_config:
            continue

        # Skip already processed SKU.
        if sku in sku_dtb_configs[model]:
            continue

        dtbs, dtbos = dtb_config
        matched_dtb = None
        for dtb, attr in dtbs.items():
            if _match_dtb(attr, sku, fw_config):
                matched_dtb = dtb
                break
        if not matched_dtb:
            raise ValueError(
                "Unable to match a dtb: "
                f"model {model}, sku {sku}, fw_config {fw_config}"
            )
        matched_dtbos = []
        for dtbo, attr in dtbos.items():
            if _match_dtb(attr, sku, fw_config):
                matched_dtbos.append(dtbo)
        sku_dtb_configs[model][sku] = (matched_dtb, matched_dtbos)

    dtb_nodes, dtbo_nodes, fit_dtb_nodes = _gen_fit_dtb_nodes(sku_dtb_configs)
    fit_config_nodes = _gen_fit_config_nodes(
        sku_dtb_configs, dtb_nodes, dtbo_nodes
    )

    return fit_dtb_nodes, fit_config_nodes

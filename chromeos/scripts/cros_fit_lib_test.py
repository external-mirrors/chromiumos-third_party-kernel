# SPDX-License-Identifier: GPL-2.0+ OR Apache-2.0 OR BSD-3-Clause

"""Unit tests for cros_fit_lib."""

import unittest
from unittest import mock

import cros_fit_lib


class TestCrosFitLib(unittest.TestCase):
    """Test class for cros_fit_lib."""

    @classmethod
    def _parse_compatible(cls, compatible_bytes):
        """Parse the compatible byte string into a set of strings."""
        return {s.decode("ascii") for s in compatible_bytes.split(b"\x00") if s}

    @classmethod
    def _get_fdt_filenames(cls, fit_config_node, fit_dtb_nodes):
        """Get the list of fdt filenames for a given config node."""
        fdt_map = {node.name: node.filename for node in fit_dtb_nodes}
        return [fdt_map[fdt] for fdt in fit_config_node.fdt]

    @mock.patch("cros_fit_lib._get_chromeos_skus")
    @mock.patch("cros_fit_lib._read_dtb_config")
    def test_process_dtb_config_with_chromeos_config(
        self, mock_read_dtb_config, mock_get_chromeos_skus
    ):
        """Test process_dtb_config() with a chromeos config file."""
        model_sku_configs = {
            "rauru": [
                cros_fit_lib.SkuConfig("rauru", 0, 0x00),
            ]
        }
        model_dtb_configs = {
            "rauru": (
                {
                    "rauru.dtb": {},
                },
                {},
            )
        }
        mock_read_dtb_config.return_value = ({}, model_dtb_configs)
        mock_get_chromeos_skus.return_value = model_sku_configs

        fit_dtb_nodes, fit_config_nodes = cros_fit_lib.process_dtb_config(
            "fake_dtb_config.yaml", "fake_chromeos_config.json"
        )

        self.assertEqual(
            len(fit_config_nodes), 1, "Wrong number of FIT config nodes"
        )
        config = fit_config_nodes[0]
        self.assertEqual(
            self._parse_compatible(config.compatible),
            {"google,rauru"},
            "Compatible string mismatch",
        )
        self.assertEqual(
            self._get_fdt_filenames(config, fit_dtb_nodes),
            ["rauru.dtb"],
            "FDT filenames mismatch",
        )

    @mock.patch("cros_fit_lib._get_chromeos_skus")
    @mock.patch("cros_fit_lib._read_dtb_config")
    def test_process_dtb_config_filter_models_from_chromeos_config(
        self, mock_read_dtb_config, mock_get_chromeos_skus
    ):
        """Test that output is filtered by models from chromeos_config."""
        model_sku_configs = {
            "rauru": [
                cros_fit_lib.SkuConfig("rauru", 0, 0x00),
            ],
            "navi": [
                cros_fit_lib.SkuConfig("navi", 0, 0x00),
            ],
        }
        filtered_model_sku_configs = {
            "rauru": [
                cros_fit_lib.SkuConfig("rauru", 0, 0x00),
            ],
        }
        model_dtb_configs = {
            "rauru": (
                {
                    "rauru.dtb": {},
                },
                {},
            ),
            "navi": (
                {
                    "navi.dtb": {},
                },
                {},
            ),
        }
        mock_read_dtb_config.return_value = (
            model_sku_configs,
            model_dtb_configs,
        )
        mock_get_chromeos_skus.return_value = filtered_model_sku_configs

        fit_dtb_nodes, fit_config_nodes = cros_fit_lib.process_dtb_config(
            "fake_dtb_config.yaml", "fake_chromeos_config.json"
        )

        self.assertEqual(
            len(fit_config_nodes), 1, "Wrong number of FIT config nodes"
        )
        config = fit_config_nodes[0]
        self.assertEqual(
            self._parse_compatible(config.compatible),
            {"google,rauru"},
            "Compatible string mismatch",
        )
        self.assertEqual(
            self._get_fdt_filenames(config, fit_dtb_nodes),
            ["rauru.dtb"],
            "FDT filenames mismatch",
        )

    @mock.patch("cros_fit_lib._read_dtb_config")
    def test_process_dtb_config_no_match(self, mock_read_dtb_config):
        """Test process_dtb_config with no matching DTB."""
        model_sku_configs = {
            "rauru": [
                cros_fit_lib.SkuConfig("rauru", 0, 0x00),
            ]
        }
        model_dtb_configs = {
            "rauru": (
                {"rauru.dtb": {"sku": [2]}},
                {},
            ),
        }
        mock_read_dtb_config.return_value = (
            model_sku_configs,
            model_dtb_configs,
        )

        with self.assertRaises(ValueError):
            cros_fit_lib.process_dtb_config("fake_dtb_config.yaml", "")

    @mock.patch("cros_fit_lib._read_dtb_config")
    def test_process_dtb_config_match_sku(self, mock_read_dtb_config):
        """Test process_dtb_config with SKU matching."""
        model_sku_configs = {
            "rauru": [
                cros_fit_lib.SkuConfig("rauru", 0, 0x00),
                cros_fit_lib.SkuConfig("rauru", 1, 0x10),
            ]
        }
        model_dtb_configs = {
            "rauru": (
                {
                    "sku0.dtb": {"sku": [0]},
                    "sku1.dtb": {"sku": [1]},
                },
                {},
            )
        }
        mock_read_dtb_config.return_value = (
            model_sku_configs,
            model_dtb_configs,
        )

        fit_dtb_nodes, fit_config_nodes = cros_fit_lib.process_dtb_config(
            "fake_dtb_config.yaml", ""
        )

        self.assertEqual(
            len(fit_config_nodes), 2, "Wrong number of FIT config nodes"
        )

        config = fit_config_nodes[0]
        self.assertEqual(
            self._parse_compatible(config.compatible),
            {"google,rauru-sku0"},
            "Compatible string mismatch for sku 0",
        )
        self.assertEqual(
            self._get_fdt_filenames(config, fit_dtb_nodes),
            ["sku0.dtb"],
            "FDT filenames mismatch for sku 0",
        )

        config = fit_config_nodes[1]
        self.assertEqual(
            self._parse_compatible(config.compatible),
            {"google,rauru-sku1"},
            "Compatible string mismatch for sku 1",
        )
        self.assertEqual(
            self._get_fdt_filenames(config, fit_dtb_nodes),
            ["sku1.dtb"],
            "FDT filenames mismatch for sku 1",
        )

    @mock.patch("cros_fit_lib._read_dtb_config")
    def test_process_dtb_config_match_fw_config(self, mock_read_dtb_config):
        """Test process_dtb_config with fw_config matching."""
        model_sku_configs = {
            "rauru": [
                cros_fit_lib.SkuConfig("rauru", 0, 0x18),
                cros_fit_lib.SkuConfig("rauru", 1, 0x28),
                cros_fit_lib.SkuConfig("rauru", 2, 0x28),
            ]
        }
        model_dtb_configs = {
            "rauru": (
                {
                    "rauru.dtb": {},
                },
                {
                    "trackpad1.dtbo": {
                        "fw_config": {"mask": 0x30, "value": 0x10},
                    },
                    "trackpad2.dtbo": {
                        "fw_config": {"mask": 0x30, "value": 0x20},
                    },
                },
            )
        }
        mock_read_dtb_config.return_value = (
            model_sku_configs,
            model_dtb_configs,
        )

        fit_dtb_nodes, fit_config_nodes = cros_fit_lib.process_dtb_config(
            "fake_dtb_config.yaml", ""
        )

        self.assertEqual(
            len(fit_config_nodes), 2, "Wrong number of FIT config nodes"
        )

        config = fit_config_nodes[0]
        self.assertEqual(
            self._parse_compatible(config.compatible),
            {"google,rauru-sku0"},
            "Compatible string mismatch for sku 0",
        )
        self.assertEqual(
            self._get_fdt_filenames(config, fit_dtb_nodes),
            ["rauru.dtb", "trackpad1.dtbo"],
            "FDT filenames mismatch for sku 0",
        )

        config = fit_config_nodes[1]
        self.assertEqual(
            self._parse_compatible(config.compatible),
            {"google,rauru-sku1", "google,rauru-sku2"},
            "Compatible string mismatch for sku 1 and 2",
        )
        self.assertEqual(
            self._get_fdt_filenames(config, fit_dtb_nodes),
            ["rauru.dtb", "trackpad2.dtbo"],
            "FDT filenames mismatch for sku 1 and 2",
        )


if __name__ == "__main__":
    unittest.main()

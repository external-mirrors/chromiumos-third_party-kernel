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
            {"google,rauru-sku0"},
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
            {"google,rauru-sku0"},
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
                {
                    "rauru.dtb": {
                        "fw_config": {"mask": 0x30, "value": 0x10},
                    },
                },
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

    @mock.patch("cros_fit_lib._read_dtb_config")
    def test_process_dtb_config_match_rev(self, mock_read_dtb_config):
        """Test process_dtb_config with rev matching."""
        model_sku_configs = {
            "rauru": [
                cros_fit_lib.SkuConfig("rauru", 0, 0x0),
                cros_fit_lib.SkuConfig("rauru", 1, 0x0),
            ]
        }
        model_dtb_configs = {
            "rauru": (
                {
                    "rauru-rev0-rev1.dtb": {
                        "rev": {"max": 1},
                    },
                    "rauru.dtb": {
                        "rev": {"min": 3},
                    },
                },
                {
                    "trackpad-rev0.dtbo": {
                        "rev": {"min": 0, "max": 0},
                    },
                    "trackpad.dtbo": {
                        "rev": {"min": 5},
                    },
                    "wwan.dtbo": {},
                },
            )
        }
        mock_read_dtb_config.return_value = (
            model_sku_configs,
            model_dtb_configs,
        )

        fit_dtb_nodes, config_nodes = cros_fit_lib.process_dtb_config(
            "fake_dtb_config.yaml", ""
        )

        self.assertEqual(
            len(config_nodes), 4, "Wrong number of FIT config nodes"
        )

        # Rev 0
        config = config_nodes[0]
        self.assertEqual(
            self._parse_compatible(config.compatible),
            {"google,rauru-rev0-sku0", "google,rauru-rev0-sku1"},
            "Compatible string mismatch for rev 0",
        )
        self.assertEqual(
            self._get_fdt_filenames(config, fit_dtb_nodes),
            ["rauru-rev0-rev1.dtb", "trackpad-rev0.dtbo", "wwan.dtbo"],
            "FDT filenames mismatch for rev 0",
        )

        # Rev 1
        config = config_nodes[1]
        self.assertEqual(
            self._parse_compatible(config.compatible),
            {"google,rauru-rev1-sku0", "google,rauru-rev1-sku1"},
            "Compatible string mismatch for rev 1",
        )
        self.assertEqual(
            self._get_fdt_filenames(config, fit_dtb_nodes),
            ["rauru-rev0-rev1.dtb", "wwan.dtbo"],
            "FDT filenames mismatch for rev 1",
        )

        # No rev 2

        # Rev 3 & 4
        config = config_nodes[2]
        self.assertEqual(
            self._parse_compatible(config.compatible),
            {
                "google,rauru-rev3-sku0",
                "google,rauru-rev3-sku1",
                "google,rauru-rev4-sku0",
                "google,rauru-rev4-sku1",
            },
            "Compatible string mismatch for rev 3 and 4",
        )
        self.assertEqual(
            self._get_fdt_filenames(config, fit_dtb_nodes),
            ["rauru.dtb", "wwan.dtbo"],
            "FDT filenames mismatch for rev 3 and 4",
        )

        # Rev 5 (latest revision)
        config = config_nodes[3]
        self.assertEqual(
            self._parse_compatible(config.compatible),
            {"google,rauru-sku0", "google,rauru-sku1"},
            "Compatible string mismatch for latest rev",
        )
        self.assertEqual(
            self._get_fdt_filenames(config, fit_dtb_nodes),
            ["rauru.dtb", "trackpad.dtbo", "wwan.dtbo"],
            "FDT filenames mismatch for latest rev",
        )


if __name__ == "__main__":
    unittest.main()

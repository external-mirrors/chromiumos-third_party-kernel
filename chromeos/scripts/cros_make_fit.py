#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
#
# Copyright 2024 Google LLC
# Written by Simon Glass <sjg@chromium.org>

"""Build a FIT containing a lot of devicetree files

Usage:
    make_fit.py -A arm64 -n 'Linux-6.6' -O linux
        -o arch/arm64/boot/image.fit -k /tmp/kern/arch/arm64/boot/image.itk
        @arch/arm64/boot/dts/dtbs-list -E -c gzip

Creates a FIT containing the supplied kernel and a set of devicetree files,
either specified individually or listed in a file (with an '@' prefix).

Use -E to generate an external FIT (where the data is placed after the
FIT data structure). This allows parsing of the data without loading
the entire FIT.

Use -c to compress the data, using bzip2, gzip, lz4, lzma, lzo and
zstd algorithms.

Use -D to decompose "composite" DTBs into their base components and
deduplicate the resulting base DTBs and DTB overlays. This requires the
DTBs to be sourced from the kernel build directory, as the implementation
looks at the .cmd files produced by the kernel build.

The resulting FIT can be booted by bootloaders which support FIT, such
as U-Boot, Linuxboot, Tianocore, etc.

Note that this tool does not yet support adding a ramdisk / initrd.
"""

import argparse
import collections
import io
import os
import subprocess
import sys
import tempfile
import time
from typing import List, Tuple

import libfdt

import cros_fit_lib


# Tool extension and the name of the command-line tools
CompTool = collections.namedtuple("CompTool", "ext,tools")

COMP_TOOLS = {
    "bzip2": CompTool(".bz2", "bzip2"),
    "gzip": CompTool(".gz", "pigz,gzip"),
    "lz4": CompTool(".lz4", "lz4"),
    "lzma": CompTool(".lzma", "lzma"),
    "lzo": CompTool(".lzo", "lzop"),
    "zstd": CompTool(".zstd", "zstd"),
}


def parse_args():
    """Parse the program ArgumentParser

    Returns:
        Namespace object containing the arguments
    """
    epilog = "Build a FIT from a directory tree containing .dtb files"
    parser = argparse.ArgumentParser(epilog=epilog, fromfile_prefix_chars="@")
    parser.add_argument(
        "-A",
        "--arch",
        type=str,
        required=True,
        help="Specifies the architecture",
    )
    parser.add_argument(
        "-c",
        "--compress",
        type=str,
        default="none",
        help="Specifies the compression",
    )
    parser.add_argument(
        "-D",
        "--decompose-dtbs",
        action="store_true",
        help="Decompose composite DTBs into base DTB and overlays",
    )
    parser.add_argument(
        "-E",
        "--external",
        action="store_true",
        help="Convert the FIT to use external data",
    )
    parser.add_argument(
        "-n", "--name", type=str, required=True, help="Specifies the name"
    )
    parser.add_argument(
        "-o",
        "--output",
        type=str,
        required=True,
        help="Specifies the output file (.fit)",
    )
    parser.add_argument(
        "-O",
        "--os",
        type=str,
        required=True,
        help="Specifies the operating system",
    )
    parser.add_argument(
        "-k",
        "--kernel",
        type=str,
        required=True,
        help="Specifies the (uncompressed) kernel input file (.itk)",
    )
    parser.add_argument(
        "--dtb-config", type=str, help="Specifies the dtb/dtbo config yaml file"
    )
    parser.add_argument(
        "--chromeos-config",
        type=str,
        help="Specifies the ChromeOS config file (required for --dtb-config)",
    )
    parser.add_argument(
        "-v", "--verbose", action="store_true", help="Enable verbose output"
    )
    parser.add_argument(
        "dtbs",
        type=str,
        nargs="*",
        help="Specifies the devicetree files to process",
    )

    return parser.parse_args()


def setup_fit(fsw: libfdt.FdtSw, name: str):
    """Make a start on writing the FIT

    Outputs the root properties and the 'images' node

    Args:
        fsw: Object to use for writing
        name: Name of kernel image
    """
    fsw.INC_SIZE = 65536
    fsw.finish_reservemap()
    fsw.begin_node("")
    fsw.property_string("description", f"{name} with devicetree set")
    fsw.property_u32("#address-cells", 1)

    fsw.property_u32("timestamp", int(time.time()))
    fsw.begin_node("images")


def write_kernel(fsw: libfdt.FdtSw, data: bytes, args: argparse.Namespace):
    """Write out the kernel image

    Writes a kernel node along with the required properties

    Args:
        fsw: Object to use for writing
        data: Data to write (possibly compressed)
        args: Contains necessary strings:
            arch: FIT architecture, e.g. 'arm64'
            fit_os: Operating Systems, e.g. 'linux'
            name: Name of OS, e.g. 'Linux-6.6.0-rc7'
            compress: Compression algorithm to use, e.g. 'gzip'
    """
    with fsw.add_node("kernel"):
        fsw.property_string("description", args.name)
        fsw.property_string("type", "kernel_noload")
        fsw.property_string("arch", args.arch)
        fsw.property_string("os", args.os)
        fsw.property_string("compression", args.compress)
        fsw.property("data", data)
        fsw.property_u32("load", 0)
        fsw.property_u32("entry", 0)


def finish_fit(fsw: libfdt.FdtSw, entries: List[Tuple]):
    """Finish the FIT ready for use

    Writes the /configurations node and subnodes

    Args:
        fsw: Object to use for writing
        entries: List of configurations:
            str: Description of model
            str: Compatible stringlist
            list of str: List of fdt node names
    """
    fsw.end_node()
    seq = 0
    with fsw.add_node("configurations"):
        for model, compat, nodes in entries:
            seq += 1
            with fsw.add_node(f"conf-{seq}"):
                fsw.property("compatible", bytes(compat))
                fsw.property_string("description", model)
                fsw.property(
                    "fdt", bytes("".join(f"{x}\x00" for x in nodes), "ascii")
                )
                fsw.property_string("kernel", "kernel")
    fsw.end_node()


def compress_data(inf: io.IOBase, compress: str) -> bytes:
    """Compress data using a selected algorithm

    Args:
        inf: Filename containing the data to compress
        compress: Compression algorithm, e.g. 'gzip'

    Returns:
        Compressed data
    """
    if compress == "none":
        return inf.read()

    comp = COMP_TOOLS.get(compress)
    if not comp:
        raise ValueError(f"Unknown compression algorithm '{compress}'")

    with tempfile.NamedTemporaryFile() as comp_fname:
        with open(comp_fname.name, "wb") as outf:
            done = False
            for tool in comp.tools.split(","):
                try:
                    subprocess.call([tool, "-c"], stdin=inf, stdout=outf)
                    done = True
                    break
                except FileNotFoundError:
                    pass
            if not done:
                raise ValueError(f"Missing tool(s): {comp.tools}\n")
            with open(comp_fname.name, "rb") as compf:
                comp_data = compf.read()
    return comp_data


def output_dtb(
    fsw: libfdt.FdtSw, node_name: str, fname: str, arch: str, compress: str
):
    """Write out a single devicetree to the FIT

    Args:
        fsw: Object to use for writing
        node_name: FDT image node name
        fname: Filename containing the DTB
        arch: FIT architecture, e.g. 'arm64'
        compress: Compressed algorithm, e.g. 'gzip'
    """
    with fsw.add_node(node_name):
        fsw.property_string("description", os.path.basename(fname))
        fsw.property_string("type", "flat_dt")
        fsw.property_string("arch", arch)
        fsw.property_string("compression", compress)

        with open(fname, "rb") as inf:
            compressed = compress_data(inf, compress)
        fsw.property("data", compressed)


def process_dtb(
    fname: str, args: argparse.Namespace
) -> Tuple[str, str, List[str]]:
    """Process an input DTB, decomposing it if requested and is possible

    Args:
        fname: Filename containing the DTB
        args: Program arguments

    Returns:
        tuple:
            str: Model name string
            str: Root compatible string
            files: list of filenames corresponding to the DTB
    """
    # Get the compatible / model information
    with open(fname, "rb") as inf:
        data = inf.read()
    fdt = libfdt.FdtRo(data)
    model = fdt.getprop(0, "model").as_str()
    compat = fdt.getprop(0, "compatible")

    if args.decompose_dtbs:
        # Check if the DTB needs to be decomposed
        path, basename = os.path.split(fname)
        cmd_fname = os.path.join(path, f".{basename}.cmd")
        # pylint: disable=encoding-missing
        with open(cmd_fname, "r", encoding="ascii") as inf:
            cmd = inf.read()

        if "scripts/dtc/fdtoverlay" in cmd:
            # This depends on the structure of the composite DTB command
            files = cmd.split()
            files = files[files.index("-i") + 1 :]
        else:
            files = [fname]
    else:
        files = [fname]

    return (model, compat, files)


def build_fit(args: argparse.Namespace) -> Tuple[bytes, int, int]:
    """Build the FIT from the provided files and arguments

    Args:
        args: Program arguments

    Returns:
        tuple:
            FIT data
            Number of configurations generated
            Total uncompressed size of data
    """
    seq = 0
    size = 0
    fsw = libfdt.FdtSw()
    setup_fit(fsw, args.name)
    entries = []
    fdts = {}

    # Handle the kernel
    with open(args.kernel, "rb") as inf:
        comp_data = compress_data(inf, args.compress)
    size += os.path.getsize(args.kernel)
    write_kernel(fsw, comp_data, args)

    if args.dtb_config:
        if args.dtbs:
            raise ValueError("List of dtbs incompatible with --dtb-config")
        fdt_nodes, config_nodes = cros_fit_lib.process_dtb_config(
            args.dtb_config,
            args.chromeos_config,
        )
        for node in fdt_nodes:
            output_dtb(fsw, node.name, node.filename, args.arch, args.compress)
        for node in config_nodes:
            entries.append([node.description, node.compatible, node.fdt])
    else:
        for fname in args.dtbs:
            # Ignore non-DTB (*.dtb) files
            if os.path.splitext(fname)[1] != ".dtb":
                continue

            (model, compat, files) = process_dtb(fname, args)

            for fn in files:
                if fn not in fdts:
                    seq += 1
                    node_name = f"fdt-{seq}"
                    size += os.path.getsize(fn)
                    output_dtb(fsw, node_name, fn, args.arch, args.compress)
                    fdts[fn] = node_name

        fdt_nodes = [fdts[fn] for fn in files]

        entries.append([model, compat, fdt_nodes])

    finish_fit(fsw, entries)

    # Include the kernel itself in the returned file count
    return fsw.as_fdt().as_bytearray(), seq + 1, size


def run_make_fit():
    """Run the tool's main logic"""
    args = parse_args()

    out_data, count, size = build_fit(args)
    with open(args.output, "wb") as outf:
        outf.write(out_data)

    ext_fit_size = None
    if args.external:
        mkimage = os.environ.get("MKIMAGE", "mkimage")
        subprocess.check_call(
            [mkimage, "-E", "-F", args.output], stdout=subprocess.DEVNULL
        )

        with open(args.output, "rb") as inf:
            data = inf.read()
        ext_fit = libfdt.FdtRo(data)
        ext_fit_size = ext_fit.totalsize()

    if args.verbose:
        comp_size = len(out_data)
        print(
            f"FIT size {comp_size:#x}/{comp_size / 1024 / 1024:.1f} MB", end=""
        )
        if ext_fit_size:
            print(
                f", header {ext_fit_size:#x}/{ext_fit_size / 1024:.1f} KB",
                end="",
            )
        print(f", {count} files, uncompressed {size / 1024 / 1024:.1f} MB")


if __name__ == "__main__":
    sys.exit(run_make_fit())

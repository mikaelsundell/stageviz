#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

"""Inspect installed macOS RenderMan binaries without loading them.

This is a preflight tool, not a render launcher. A passing check establishes
necessary conditions only; compiler ABI, delegate behavior, device support and
licensing still require integration validation with a compatible build.
"""

import argparse
import json
import platform
import re
import struct
import subprocess
from pathlib import Path


def macho_architectures(path):
    """Read thin/fat Mach-O headers, including universal and byte-swapped files."""
    cpu_names = {0x01000007: "x86_64", 0x0100000C: "arm64", 7: "i386", 12: "arm"}
    with Path(path).open("rb") as stream:
        magic = stream.read(4)
        thin = {b"\xcf\xfa\xed\xfe": "<", b"\xce\xfa\xed\xfe": "<",
                b"\xfe\xed\xfa\xcf": ">", b"\xfe\xed\xfa\xce": ">"}
        fat = {b"\xca\xfe\xba\xbe": (">", 20), b"\xbe\xba\xfe\xca": ("<", 20),
               b"\xca\xfe\xba\xbf": (">", 32), b"\xbf\xba\xfe\xca": ("<", 32)}
        if magic in thin:
            cpu = struct.unpack(thin[magic] + "I", stream.read(4))[0]
            return [cpu_names.get(cpu, "cpu-0x%x" % cpu)]
        if magic not in fat:
            raise ValueError("Not a Mach-O binary: %s" % path)
        endian, entry_size = fat[magic]
        count = struct.unpack(endian + "I", stream.read(4))[0]
        if count > 64:
            raise ValueError("Invalid universal binary architecture count")
        architectures = []
        for _ in range(count):
            entry = stream.read(entry_size)
            if len(entry) != entry_size:
                raise ValueError("Truncated universal binary header")
            cpu = struct.unpack(endian + "I", entry[:4])[0]
            architectures.append(cpu_names.get(cpu, "cpu-0x%x" % cpu))
        return sorted(set(architectures))


def inspect_binary(path):
    result = {"path": str(path), "architectures": [], "error": None}
    try:
        result["architectures"] = macho_architectures(path)
    except (OSError, ValueError, struct.error) as error:
        result["error"] = str(error)
    return result


def tool_output(command):
    try:
        return subprocess.run(command, check=True, capture_output=True, text=True, timeout=30).stdout
    except (OSError, subprocess.SubprocessError) as error:
        raise RuntimeError("Cannot inspect binary with %s: %s" % (command[0], error)) from error


def inspect_delegate(loader, runtime, architecture, usd_version, python_version):
    """Check loader, delegate and runtime. Never dlopen or register the plugin."""
    binaries = [inspect_binary(path) for path in
                (loader, loader.parent / "hdPrman.dylib", runtime / "lib" / "libprman.dylib")]
    problems = []
    for binary in binaries:
        if binary["error"]:
            problems.append(binary["error"])
        elif architecture not in binary["architectures"]:
            problems.append("%s is %s; Stageviz requires %s" %
                            (binary["path"], "/".join(binary["architectures"]), architecture))
    observed_usd = set()
    observed_python = set()
    # Check both loader and delegate; mismatches in either are fatal.
    for path in (loader, loader.parent / "hdPrman.dylib"):
        if not path.is_file():
            continue
        try:
            symbols = tool_output(["/usr/bin/nm", "-u", str(path)])
            observed_usd.update("%s.%s" % (int(minor), int(patch)) for minor, patch in
                                re.findall(r"pxrInternal_v0_(\d+)_(\d+)__", symbols))
            dependencies = tool_output(["/usr/bin/otool", "-L", str(path)])
            observed_python.update(re.findall(
                r"Python\.framework/Python.*current version (\d+\.\d+)\.", dependencies))
        except RuntimeError as error:
            problems.append(str(error))
    if not observed_usd:
        problems.append("Could not verify the plugin's USD ABI from imported symbols")
    elif observed_usd != {usd_version}:
        problems.append("Plugin imports USD %s; Stageviz requires USD %s" %
                        (", ".join(sorted(observed_usd)), usd_version))
    if not observed_python:
        problems.append("Could not verify the plugin's Python ABI from dependencies")
    elif observed_python != {python_version}:
        problems.append("Plugin links Python %s; Stageviz requires Python %s" %
                        (", ".join(sorted(observed_python)), python_version))
    return {"loader": str(loader), "runtime": str(runtime), "binaries": binaries,
            "usd_versions": sorted(observed_usd), "python_versions": sorted(observed_python),
            "passes_initial_checks": not problems, "problems": problems}


def inspect_installation(pixar_root, architecture, usd_version, python_version, runtime=None):
    root = Path(pixar_root)
    runtimes = [Path(runtime)] if runtime else sorted(root.glob("RenderManProServer-*"))
    candidates = []
    for loader in sorted(root.glob("RenderManForMaya-*/lib/*/usd_plugins_*/hdPrmanLoader.dylib")):
        version = loader.relative_to(root).parts[0].removeprefix("RenderManForMaya-")
        # Never silently pair a Maya plugin with a different RenderMan release.
        matching = [path for path in runtimes if path.name == "RenderManProServer-" + version]
        for server in matching:
            candidates.append(inspect_delegate(loader, server, architecture, usd_version, python_version))
    return {"target": {"architecture": architecture, "usd_version": usd_version,
                       "python_version": python_version}, "candidates": candidates,
            "passes_initial_checks": any(item["passes_initial_checks"] for item in candidates)}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pixar-root", type=Path, default=Path("/Applications/Pixar"))
    parser.add_argument("--runtime", type=Path, help="RenderManProServer directory for the same release as the plugin")
    parser.add_argument("--architecture", default=platform.machine(), choices=("arm64", "x86_64"))
    parser.add_argument("--usd-version", default="25.11", help="Stageviz's compiled USD version (default: 25.11)")
    parser.add_argument("--python-version", default="3.9", help="Stageviz's embedded Python version (default: 3.9)")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    report = inspect_installation(args.pixar_root, args.architecture, args.usd_version,
                                  args.python_version, args.runtime)
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print("Stageviz target: {architecture}, USD {usd_version}, Python {python_version}".format(**report["target"]))
        if not report["candidates"]:
            print("No matching RenderMan for Maya Hydra plugin and Pro Server pair found.")
        for candidate in report["candidates"]:
            print("\n%s" % candidate["loader"])
            for problem in candidate["problems"]:
                print("  BLOCKED: %s" % problem)
            if candidate["passes_initial_checks"]:
                print("  Passes initial binary checks; integration validation is still required.")
        print("\nNo libraries loaded. No license requested. This tool does not start a render.")
    return 0 if report["passes_initial_checks"] else 1


if __name__ == "__main__":
    raise SystemExit(main())

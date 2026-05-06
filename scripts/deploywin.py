# Copyright 2022-present Contributors to the stageviz project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/mikaelsundell/stageviz

import fnmatch
import os
import shutil
import sys
import pefile


copied_binaries = set()
missing_dlls = set()
checked_paths = set()
copied_directories = set()
missing_directories = set()
copied_python_packages = set()
missing_python_packages = set()

excluded_dlls = {  # lowercase
    "advapi32.dll",
    "api-ms-win-",  # exclude all api-ms-win-* system DLLs
    "authz.dll",
    "bcrypt.dll",
    "comctl32.dll",
    "comdlg32.dll",
    "crypt32.dll",
    "d3d",
    "D3Dcompiler",
    "dbg",
    "dnsapi.dll",
    "dwrite.dll",
    "dwmapi.dll",
    "dxgi.dll",
    "gdi32.dll",
    "glu32.dll",
    "imm32.dll",
    "iphlpapi.dll",
    "kernel32.dll",
    "kernelbase.dll",
    "mpr.dll",
    "msasn1.dll",
    "msvcrt.dll",
    "netapi32.dll",
    "ntdll.dll",
    "ole32.dll",
    "oleaut32.dll",
    "rpcrt4.dll",
    "secur32.dll",
    "setupapi.dll",
    "shell32.dll",
    "shlwapi.dll",
    "urlmon.dll",
    "user32.dll",
    "userenv.dll",
    "uxtheme.dll",
    "version.dll",
    "win32u.dll",
    "winhttp.dll",
    "wininet.dll",
    "winmm.dll",
    "winscard.dll",
    "winspool.drv",
    "ws2_32.dll",
}

# copy all matching DLLs from search paths.
extra_dll_patterns = [
    "usd_*.dll",
]


def norm(path: str) -> str:
    return os.path.normcase(os.path.abspath(path))


def should_exclude_dll(dll_name: str) -> bool:
    dll_name_lower = dll_name.lower()
    if dll_name_lower in excluded_dlls:
        return True
    for prefix in excluded_dlls:
        if dll_name_lower.startswith(prefix):
            return True
    return False


def should_skip_dir(dirname: str) -> bool:
    return dirname.lower() == "__pycache__"


def should_skip_python_file(filename: str) -> bool:
    lower = filename.lower()
    return (
        lower.endswith(".dll")
        or lower.endswith(".lib")
        or lower.endswith(".exe")
        or lower.endswith(".pyc")
    )


def should_skip_copied_directory_file(filename: str, mode: str) -> bool:
    lower = filename.lower()

    # plugin/usd trees and similar should be copied as-is, but keep import libs out
    if mode == "plugins":
        return lower.endswith(".lib")

    return False


def split_list_arg(value: str):
    if not value:
        return []
    return [os.path.normpath(item) for item in value.split(";") if item.strip()]


def parse_directory_specs(value: str):
    r"""
    Parse semicolon-separated directory specs.

    Supported forms:
      <src>|<dest-relative>
      <src>|<dest-relative>|<mode>

    Example:
      C:\prefix\plugin\usd|plugin\usd|plugins
      C:\prefix\lib\usd|usd
    """
    specs = []
    if not value:
        return specs

    for item in value.split(";"):
        item = item.strip()
        if not item:
            continue

        parts = [part.strip() for part in item.split("|")]
        if len(parts) not in (2, 3):
            print(
                f"Warning: Invalid directory spec '{item}', "
                "expected '<src>|<dest-relative>' or '<src>|<dest-relative>|<mode>'"
            )
            continue

        src = os.path.normpath(parts[0])
        dest_rel = os.path.normpath(parts[1])
        mode = parts[2] if len(parts) == 3 else "default"

        if not src or not dest_rel:
            print(f"Warning: Invalid directory spec '{item}'")
            continue

        specs.append((src, dest_rel, mode))

    return specs


def find_dll_dependencies(binary_path: str):
    dlls = set()
    try:
        pe = pefile.PE(binary_path)
        if hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
            for entry in pe.DIRECTORY_ENTRY_IMPORT:
                dll_name = entry.dll.decode("utf-8")
                if should_exclude_dll(dll_name):
                    print(f"Skipping system DLL: {dll_name}")
                else:
                    dlls.add(dll_name)
        pe.close()
    except Exception as exc:
        print(f"Error processing {binary_path}: {exc}")
    return dlls


def find_dll_in_paths(dll_name: str, search_paths):
    for path in search_paths:
        checked_paths.add(norm(path))

        direct_path = os.path.join(path, dll_name)
        if os.path.isfile(direct_path):
            return direct_path

        try:
            for entry in os.listdir(path):
                entry_path = os.path.join(path, entry)
                if not os.path.isdir(entry_path):
                    continue
                checked_paths.add(norm(entry_path))
                nested_path = os.path.join(entry_path, dll_name)
                if os.path.isfile(nested_path):
                    return nested_path
        except OSError:
            continue

    return None


def find_matching_dlls_in_paths(pattern: str, search_paths):
    matches = []
    seen = set()
    pattern_lower = pattern.lower()

    for path in search_paths:
        checked_paths.add(norm(path))

        try:
            for entry in os.listdir(path):
                entry_path = os.path.join(path, entry)

                if os.path.isfile(entry_path):
                    entry_lower = entry.lower()
                    if fnmatch.fnmatch(entry_lower, pattern_lower):
                        key = norm(entry_path)
                        if key not in seen:
                            seen.add(key)
                            matches.append(entry_path)

                elif os.path.isdir(entry_path):
                    checked_paths.add(norm(entry_path))
                    try:
                        for nested in os.listdir(entry_path):
                            nested_path = os.path.join(entry_path, nested)
                            if not os.path.isfile(nested_path):
                                continue

                            nested_lower = nested.lower()
                            if fnmatch.fnmatch(nested_lower, pattern_lower):
                                key = norm(nested_path)
                                if key not in seen:
                                    seen.add(key)
                                    matches.append(nested_path)
                    except OSError:
                        continue

        except OSError:
            continue

    return matches


def ensure_directory(path: str):
    os.makedirs(path, exist_ok=True)


def copy_file(src: str, dest_dir: str):
    ensure_directory(dest_dir)
    dest_file = os.path.join(dest_dir, os.path.basename(src))
    dest_key = norm(dest_file)

    if dest_key in copied_binaries:
        return dest_file

    try:
        shutil.copy2(src, dest_file)
        copied_binaries.add(dest_key)
        print(f"Copied: {src} -> {dest_file}")
        return dest_file
    except Exception as exc:
        print(f"Failed to copy {src} -> {dest_file}: {exc}")
        return None


def collect_search_paths(extra_paths=None):
    search_paths = []

    if extra_paths:
        search_paths.extend(extra_paths)

    for path in os.environ.get("PATH", "").split(";"):
        path = path.strip()
        if path:
            search_paths.append(os.path.normpath(path))

    deduped = []
    seen = set()
    for path in search_paths:
        key = norm(path)
        if key in seen:
            continue
        seen.add(key)
        deduped.append(path)
    return deduped


def copy_binary_dependencies(binary_path: str, dest_dir: str, search_paths, processed=None, copy_self=True):
    """
    Copy DLL dependencies for a binary into dest_dir.

    copy_self=True:
        copy the binary itself first, then recurse its DLL dependencies.
        Used for the main executable.

    copy_self=False:
        do not copy the binary itself, only inspect it and copy discovered
        DLL dependencies into dest_dir.
        Used when scanning already copied package/plugin trees.
    """
    if processed is None:
        processed = set()

    binary_key = norm(binary_path)
    if binary_key in processed:
        return
    processed.add(binary_key)

    if copy_self:
        copied_binary_path = copy_file(binary_path, dest_dir)
        if copied_binary_path is None:
            return

    for dll_name in sorted(find_dll_dependencies(binary_path)):
        dll_path = find_dll_in_paths(dll_name, search_paths)
        if not dll_path:
            print(f"Warning: {dll_name} not found in search paths or system PATH")
            missing_dlls.add(dll_name)
            continue

        if not dll_path.lower().endswith(".dll"):
            continue

        copied_dll_path = copy_file(dll_path, dest_dir)
        if copied_dll_path:
            copy_binary_dependencies(dll_path, dest_dir, search_paths, processed, copy_self=False)


def copy_dependencies(exe_path: str, dest_dir: str, extra_paths=None):
    print("\nCopying executable and dependencies...\n")
    search_paths = collect_search_paths(extra_paths)
    copy_binary_dependencies(exe_path, dest_dir, search_paths, copy_self=True)


def scan_directory_for_binary_dependencies(src_dir: str, dest_dir: str, extra_paths=None):
    """
    Scan a copied directory tree for binaries and copy their discovered DLL
    dependencies into the deploy root.
    """
    print(f"\nScanning binaries in: {src_dir}\n")
    search_paths = collect_search_paths(extra_paths)
    processed = set()

    for root, dirs, files in os.walk(src_dir):
        dirs[:] = [d for d in dirs if not should_skip_dir(d)]

        for filename in files:
            lower = filename.lower()
            if lower.endswith(".dll") or lower.endswith(".pyd") or lower.endswith(".exe"):
                binary_path = os.path.join(root, filename)
                print(f"Scanning binary: {binary_path}")
                copy_binary_dependencies(binary_path, dest_dir, search_paths, processed, copy_self=False)


def copy_extra_dependencies(dest_dir: str, search_paths):
    print("\nCopying explicitly requested extra dependencies...\n")

    for pattern in extra_dll_patterns:
        matches = find_matching_dlls_in_paths(pattern, search_paths)

        if not matches:
            print(f"Warning: No DLLs matched pattern {pattern}")
            missing_dlls.add(pattern)
            continue

        for dll_path in sorted(matches):
            copy_file(dll_path, dest_dir)


def make_copytree_ignore(mode: str):
    def _ignore(src, names):
        ignored = []
        for name in names:
            full_path = os.path.join(src, name)

            if os.path.isdir(full_path):
                if should_skip_dir(name):
                    ignored.append(name)
                continue

            if should_skip_copied_directory_file(name, mode):
                ignored.append(name)

        return ignored

    return _ignore


def copy_directory_to_relative_dest(src: str, dest_root: str, dest_relative: str, mode: str = "default"):
    if not os.path.isdir(src):
        print(f"Warning: Directory not found: {src}")
        missing_directories.add(src)
        return None

    final_dest = os.path.join(dest_root, dest_relative)
    final_dest_key = norm(final_dest)

    if final_dest_key in copied_directories:
        return final_dest

    ensure_directory(os.path.dirname(final_dest))
    try:
        shutil.copytree(
            src,
            final_dest,
            dirs_exist_ok=True,
            ignore=make_copytree_ignore(mode),
        )
        copied_directories.add(final_dest_key)
        print(f"Copied directory [{mode}]: {src} -> {final_dest}")
        return final_dest
    except Exception as exc:
        print(f"Failed to copy directory {src} -> {final_dest}: {exc}")
        return None


def copy_requested_directories(dest_root: str, directory_specs):
    print("\nCopying requested directories...\n")
    copied_paths = []

    for src, dest_relative, mode in directory_specs:
        copied_path = copy_directory_to_relative_dest(src, dest_root, dest_relative, mode)
        if copied_path:
            copied_paths.append(copied_path)

    return copied_paths


def find_python_package(package_name: str, python_roots):
    for root in python_roots:
        checked_paths.add(norm(root))
        candidate = os.path.join(root, package_name)
        if os.path.isdir(candidate):
            return candidate
    return None


def copy_python_package_tree(src: str, dst: str):
    if os.path.exists(dst):
        shutil.rmtree(dst)

    for root, dirs, files in os.walk(src):
        dirs[:] = [d for d in dirs if not should_skip_dir(d)]

        rel = os.path.relpath(root, src)
        dst_root = dst if rel == "." else os.path.join(dst, rel)
        ensure_directory(dst_root)

        for filename in files:
            if should_skip_python_file(filename):
                continue

            src_file = os.path.join(root, filename)
            dst_file = os.path.join(dst_root, filename)
            shutil.copy2(src_file, dst_file)


def copy_python_packages(dest_root: str, python_roots, python_packages):
    print("\nCopying python packages...\n")

    site_packages_dst = os.path.join(dest_root, "site-packages")
    ensure_directory(site_packages_dst)

    copied_package_paths = []
    package_search_paths = []

    for package_name in python_packages:
        package_src = find_python_package(package_name, python_roots)
        if not package_src:
            print(f"Warning: Python package '{package_name}' not found")
            missing_python_packages.add(package_name)
            continue

        package_dst = os.path.join(site_packages_dst, package_name)
        try:
            copy_python_package_tree(package_src, package_dst)
            copied_python_packages.add(package_name)
            copied_package_paths.append(package_dst)
            package_search_paths.append(package_src)
            print(f"Copied python package: {package_src} -> {package_dst}")
        except Exception as exc:
            print(f"Failed to copy python package {package_src} -> {package_dst}: {exc}")

    return copied_package_paths, package_search_paths


def report_summary():
    print("\n\nSummary of operations:")
    print("------------------------")

    print("Copied binaries:")
    for item in sorted(copied_binaries):
        print(f"  - {item}")

    if missing_dlls:
        print("\nMissing DLLs:")
        for item in sorted(missing_dlls):
            print(f"  - {item}")
    else:
        print("\nNo missing DLLs.")

    print("\nCopied directories:")
    for item in sorted(copied_directories):
        print(f"  - {item}")

    if missing_directories:
        print("\nMissing directories:")
        for item in sorted(missing_directories):
            print(f"  - {item}")
    else:
        print("\nNo missing directories.")

    print("\nCopied python packages:")
    for item in sorted(copied_python_packages):
        print(f"  - {item}")

    if missing_python_packages:
        print("\nMissing python packages:")
        for item in sorted(missing_python_packages):
            print(f"  - {item}")
    else:
        print("\nNo missing python packages.")

    print("\nChecked paths:")
    for item in sorted(checked_paths):
        print(f"  - {item}")

    print("\nOperation completed.\n")


def main():
    if len(sys.argv) < 3:
        print(
            "Usage: python deploywin.py "
            "<path-to-exe> <destination-directory> "
            "[search-paths] [copy-dirs] [python-roots] [python-packages]"
        )
        sys.exit(1)

    exe_path = os.path.normpath(sys.argv[1])
    dest_dir = os.path.normpath(sys.argv[2])

    if not os.path.isfile(exe_path):
        print(f"Error: Executable not found: {exe_path}")
        sys.exit(1)

    extra_paths = split_list_arg(sys.argv[3]) if len(sys.argv) >= 4 else []
    directory_specs = parse_directory_specs(sys.argv[4]) if len(sys.argv) >= 5 else []
    python_roots = split_list_arg(sys.argv[5]) if len(sys.argv) >= 6 else []
    python_packages = [item for item in (sys.argv[6].split(";") if len(sys.argv) >= 7 else []) if item.strip()]

    ensure_directory(dest_dir)

    search_paths = collect_search_paths(extra_paths)

    copy_dependencies(exe_path, dest_dir, extra_paths)
    copy_extra_dependencies(dest_dir, search_paths)

    copied_dirs = copy_requested_directories(dest_dir, directory_specs)
    for copied_dir in copied_dirs:
        scan_directory_for_binary_dependencies(copied_dir, dest_dir, extra_paths)

    copied_package_dirs, package_search_paths = copy_python_packages(dest_dir, python_roots, python_packages)

    package_extra_paths = list(extra_paths) + package_search_paths
    for package_dir in copied_package_dirs:
        scan_directory_for_binary_dependencies(package_dir, dest_dir, package_extra_paths)

    report_summary()


if __name__ == "__main__":
    main()
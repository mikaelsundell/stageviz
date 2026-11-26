#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Generate Finder associations from the readable formats in the native USD SDK."""
import argparse
import json
import os
from pathlib import Path
import plistlib
import stat
import subprocess
import sys
import tempfile


def readable_extensions(tool, bundle, configuration, deploy_build=False):
    environment = dict(os.environ)
    debug = configuration.lower() == 'debug'
    if debug:
        environment['DYLD_IMAGE_SUFFIX'] = '_debug'
    else:
        environment.pop('DYLD_IMAGE_SUFFIX', None)
    command = [str(tool)]
    # Application uses bundled plugin directories for deployment and NDEBUG builds.
    if deploy_build or not debug:
        for relative in ('Contents/PlugIns/usd', 'Contents/Frameworks/usd'):
            command.extend(['--plugin-dir', str(bundle / relative)])
    process = subprocess.run(command, env=environment, text=True, encoding='utf-8',
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
    if process.returncode:
        raise RuntimeError('USD format discovery failed: ' + process.stderr.strip())
    values = json.loads(process.stdout)['extensions']
    if not isinstance(values, list) or not values or any(not isinstance(e, str) or not e for e in values):
        raise ValueError('USD must report a nonempty list of readable extensions')
    return sorted(set(values))


def update_document_types(path, extensions, check=False):
    original = path.read_bytes()
    document = plistlib.loads(original)
    types = document.get('CFBundleDocumentTypes')
    if not isinstance(types, list) or len(types) != 1 or not isinstance(types[0], dict):
        raise ValueError('Expected one Stageviz document-type declaration in ' + str(path))
    if types[0].get('CFBundleTypeExtensions') == extensions:
        return False
    if check:
        raise ValueError('Finder associations differ from USD; rebuild the Stageviz bundle')
    types[0]['CFBundleTypeExtensions'] = extensions
    # Preserve the bundle's plist representation and permissions; publish atomically.
    encoding = plistlib.FMT_BINARY if original.startswith(b'bplist') else plistlib.FMT_XML
    encoded = plistlib.dumps(document, fmt=encoding, sort_keys=False)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix='.Info.plist-', delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(encoded)
        temporary.chmod(stat.S_IMODE(path.stat().st_mode))
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--formats-tool', required=True, type=Path)
    parser.add_argument('--bundle', required=True, type=Path)
    parser.add_argument('--configuration', required=True)
    parser.add_argument('--deploy-build', action='store_true')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    extensions = readable_extensions(args.formats_tool.resolve(), args.bundle.resolve(),
                                     args.configuration, args.deploy_build)
    changed = update_document_types(args.bundle / 'Contents/Info.plist', extensions, args.check)
    print('Finder associations %s: %s' % ('updated' if changed else 'verified', ', '.join(extensions)))


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, KeyError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)

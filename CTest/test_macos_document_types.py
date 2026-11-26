#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Focused plist checks, with optional native USD discovery integration."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import plistlib
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('document_types', ROOT / 'scripts/update_macos_document_types.py')
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class DocumentTypesTest(unittest.TestCase):
    def test_preserves_metadata_and_is_idempotent(self):
        for encoding in (plistlib.FMT_XML, plistlib.FMT_BINARY):
            with self.subTest(encoding=encoding), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'Info.plist'
                document = {'CFBundleIdentifier': 'test.stageviz', 'CFBundleDocumentTypes': [{
                    'CFBundleTypeName': 'USD', 'CFBundleTypeRole': 'Editor',
                    'LSHandlerRank': 'Owner', 'CFBundleTypeExtensions': []}]}
                path.write_bytes(plistlib.dumps(document, fmt=encoding))
                path.chmod(0o644)
                self.assertTrue(generator.update_document_types(path, ['sample']))
                document['CFBundleDocumentTypes'][0]['CFBundleTypeExtensions'] = ['sample']
                self.assertEqual(plistlib.loads(path.read_bytes()), document)
                self.assertEqual(path.stat().st_mode & 0o777, 0o644)
                self.assertEqual(path.read_bytes().startswith(b'bplist'), encoding == plistlib.FMT_BINARY)
                before = (path.read_bytes(), path.stat().st_mtime_ns)
                self.assertFalse(generator.update_document_types(path, ['sample'], check=True))
                self.assertFalse(generator.update_document_types(path, ['sample']))
                with self.assertRaises(ValueError):
                    generator.update_document_types(path, ['different'], check=True)
                self.assertEqual((path.read_bytes(), path.stat().st_mtime_ns), before)

    def test_discovery_environment_and_validation(self):
        response = subprocess.CompletedProcess([], 0, '{"extensions":["z","a","z"]}', '')
        with patch.dict(os.environ, {'DYLD_IMAGE_SUFFIX': '_debug'}), \
                patch.object(generator.subprocess, 'run', return_value=response) as run:
            self.assertEqual(generator.readable_extensions(Path('/tool'), Path('/bundle'), 'Release'), ['a', 'z'])
            self.assertNotIn('DYLD_IMAGE_SUFFIX', run.call_args.kwargs['env'])
            self.assertIn('/bundle/Contents/PlugIns/usd', run.call_args.args[0])
            generator.readable_extensions(Path('/tool'), Path('/bundle'), 'Debug')
            self.assertEqual(run.call_args.kwargs['env']['DYLD_IMAGE_SUFFIX'], '_debug')
            self.assertEqual(run.call_args.args[0], ['/tool'])
            generator.readable_extensions(Path('/tool'), Path('/bundle'), 'Debug', deploy_build=True)
            self.assertIn('/bundle/Contents/Frameworks/usd', run.call_args.args[0])
            for output in ('{"extensions":[]}', '{"extensions":[1]}', '{"extensions":"usd"}', '{}', 'invalid'):
                run.return_value = subprocess.CompletedProcess([], 0, output, '')
                with self.assertRaises((ValueError, KeyError)):
                    generator.readable_extensions(Path('/tool'), Path('/bundle'), 'Debug')
            run.return_value = subprocess.CompletedProcess([], 1, '', 'discovery failure')
            with self.assertRaisesRegex(RuntimeError, 'discovery failure'):
                generator.readable_extensions(Path('/tool'), Path('/bundle'), 'Debug')


def integration(tool, bundle, configuration, deploy_build):
    extensions = generator.readable_extensions(tool, bundle, configuration, deploy_build)
    generator.update_document_types(bundle / 'Contents/Info.plist', extensions, check=True)
    with tempfile.TemporaryDirectory(prefix='stageviz-finder-') as directory:
        root = Path(directory)
        marker = root / 'startup-ran'
        (root / 'stageviz.py').write_text('from pathlib import Path\nPath(%r).touch()\n' % str(marker))
        metadata = root / 'metadata'
        metadata.mkdir()
        fixture_bundle = root / 'Fixture.app'
        plugin_parent = fixture_bundle / 'Contents/PlugIns'
        plugin_parent.mkdir(parents=True)
        (plugin_parent / 'usd').symlink_to(metadata, target_is_directory=True)
        types = {}
        for suffix, readable in (('read', True), ('write', False)):
            types['StagevizFinderFixture' + suffix] = {
                'bases': ['SdfFileFormat'], 'extensions': ['svfinder' + suffix],
                'formatId': 'stageviz_finder_fixture_' + suffix, 'target': 'usd', 'primary': True,
                'supportsReading': readable, 'supportsWriting': not readable, 'supportsEditing': False,
            }
        # Release/deployment discovery disables standard SDK/environment search
        # paths when bundle plugins exist. Inject through an isolated bundle's
        # explicit plugin directory and include the original bundle metadata.
        includes = [str(path.resolve()) + '/' for path in
                    (bundle / 'Contents/PlugIns/usd', bundle / 'Contents/Frameworks/usd')
                    if path.is_dir()]
        (metadata / 'plugInfo.json').write_text(json.dumps({'Includes': includes, 'Plugins': [{
            'Type': 'resource', 'Name': 'stageviz_finder_fixture', 'Root': '.', 'ResourcePath': '.',
            'Info': {'Types': types},
        }]}))
        existing = os.environ.get('PXR_PLUGINPATH_NAME', '')
        with patch.dict(os.environ, {'HOME': str(root), 'PXR_PLUGINPATH_NAME': str(metadata) +
                                    (os.pathsep + existing if existing else '')}):
            discovered = generator.readable_extensions(tool, fixture_bundle, configuration, deploy_build)
        if includes:
            assert set(extensions).issubset(discovered), (extensions, discovered)
        assert 'svfinderread' in discovered, discovered
        assert 'svfinderwrite' not in discovered, discovered
        assert not marker.exists(), 'Native discovery executed user startup Python'
        # Test generated metadata without placing synthetic extensions in the real app.
        destination = root / 'Info.plist'
        destination.write_bytes((bundle / 'Contents/Info.plist').read_bytes())
        generator.update_document_types(destination, discovered)
        assert plistlib.loads(destination.read_bytes())['CFBundleDocumentTypes'][0]['CFBundleTypeExtensions'] == discovered
    print('Native USD discovery and bundle associations verified:', ', '.join(extensions))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--formats-tool', type=Path)
    parser.add_argument('--bundle', type=Path)
    parser.add_argument('--configuration', default='Debug')
    parser.add_argument('--deploy-build', action='store_true')
    args = parser.parse_args()
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(DocumentTypesTest))
    if not result.wasSuccessful():
        raise SystemExit(1)
    if args.formats_tool:
        integration(args.formats_tool.resolve(), args.bundle.resolve(), args.configuration, args.deploy_build)

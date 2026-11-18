#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Verify OCIO imports and processor creation from a deployed disposable app.

Example: python3 CTest/benchmark/check_ocio_deployment.py --app APP_EXECUTABLE
Development builds are intentionally rejected: the extension must come from
inside the packaged app, rather than the configured dependency installation.
"""
import argparse
import json
from pathlib import Path
import tempfile

from mcp_runtime import DisposableStageviz


def run(args):
    app = Path(args.app).resolve()
    package_root = app.parent.parent if app.parent.name == 'MacOS' else app.parent
    output = Path(args.output or tempfile.mkdtemp(prefix='stageviz-ocio-deploy-'))
    # DisposableStageviz creates its own directory and refuses to reuse evidence.
    if not args.output:
        output = output / 'process'
    with DisposableStageviz(app, output, port=args.port) as runtime:
        result = runtime.python('''import json,sys
from pathlib import Path
import stageviz
import PyOpenColorIO as OCIO
import PyOpenColorIO.PyOpenColorIO as extension
from pxr import UsdImagingGL
assert UsdImagingGL.Engine.GetRendererPlugins(), 'Bundled USD renderer plugins are not registered'
root=Path(%r).resolve()
def inside(path):
    return any(root.samefile(parent) for parent in Path(path).resolve().parents)
module=Path(extension.__file__).resolve()
assert inside(module), ('OCIO extension is outside deployed app', str(module))
config=OCIO.Config.CreateFromBuiltinConfig('studio-config-v2.2.0_aces-v1.3_ocio-v2.4')
assert stageviz.session().viewState().colorViews()==list(config.getViews('sRGB - Display'))
processor=config.getProcessor('Linear Rec.709 (sRGB)','sRGB Encoded Rec.709 (sRGB)').getDefaultCPUProcessor()
pixel=processor.applyRGB([0.18,0.18,0.18])
assert all(abs(value-0.461356)<0.0001 for value in pixel), pixel
loaded=[]
if sys.platform=='darwin':
    import ctypes
    dyld=ctypes.CDLL(None)
    dyld._dyld_image_count.restype=ctypes.c_uint32
    dyld._dyld_get_image_name.argtypes=[ctypes.c_uint32]
    dyld._dyld_get_image_name.restype=ctypes.c_char_p
    loaded=[dyld._dyld_get_image_name(i).decode() for i in range(dyld._dyld_image_count())]
    loaded=[name for name in loaded if '/libOpenColorIO.' in name]
    assert len(loaded)==1 and inside(loaded[0]), loaded
print(json.dumps(dict(version=OCIO.GetVersion(),extension=str(module),libraries=loaded,gray=pixel)))
''' % str(package_root))
        (output / 'ocio.json').write_text(json.dumps(result, indent=2))
        if args.render_check:
            suite = Path(__file__).resolve().parents[1] / 'python/testcolormanagement.py'
            render = runtime.python("import runpy; ns=runpy.run_path(%r); ns['run'](disposable=True)" % str(suite))
            (output / 'render.json').write_text(json.dumps(render, indent=2))
        (output / 'stage.json').write_text(json.dumps(runtime.oracle(), indent=2))
        print('PASS deployed OCIO import and processor: ' + str(output))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', required=True)
    parser.add_argument('--output')
    parser.add_argument('--render-check', action='store_true', help='Also run the embedded viewport color regression')
    parser.add_argument('--port', type=int, default=37715)
    run(parser.parse_args())

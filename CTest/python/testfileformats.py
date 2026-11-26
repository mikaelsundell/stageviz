# SPDX-License-Identifier: BSD-3-Clause
"""External conversion callbacks, isolated USD import, UI options and undo."""
import json
import gc
import inspect
import os
from pathlib import Path
import runpy

import stageviz
from pxr import Sdf, Usd, UsdGeom

_base = runpy.run_path(str(Path(__file__).with_name('testviewport.py')),
                      init_globals={'_VIEWPORT_AUTORUN': False})
tb = _base['tb']


class FileFormatsTest(_base['ViewportTest']):
    def setup(self):
        assert self.disposable
        self.original_filename = stageviz.session().filename()
        self.original_selection = list(stageviz.session().paths())
        self.plugin = runpy.run_path(str(Path(__file__).resolve().parents[2] / 'scripts/fileformats/simple_shapes.py'))
        self.plugin['register']()
        self.source = self.directory / 'input.svshape'
        self.source.write_text('{"radius": 2.0}')
        self.option_calls = []
        self.cancel = False
        self.fail = False
        self.load_calls = 0
        self.options_token = object()
        self.native_fixture = False

    def native_formats(self):
        extension = os.environ.get('STAGEVIZ_TEST_NATIVE_USD_FORMAT')
        self.native_fixture = bool(extension)
        if not extension:
            print('Native Sdf fixture requires the external controller to configure plugins before startup')
            return
        assert extension in Sdf.FileFormat.FindAllFileFormatExtensions()
        assert '*.' + extension in stageviz.plugins._filters()
        assert '*.' + extension not in stageviz.plugins._filters(True)
        try:
            stageviz.plugins.registerFileFormat('Conflicting USD extension', [extension], loader=self.plugin['load'])
        except ValueError:
            pass
        else:
            raise AssertionError('Stageviz converter replaced a registered native USD extension')

    def registry(self):
        formats = stageviz.plugins
        assert all(inspect.isbuiltin(getattr(formats, name))
                   for name in ('registerFileFormat', 'unregisterFileFormat', 'fileFormats', 'readFile', 'writeFile'))
        assert not hasattr(formats, '_native'), 'public functions are direct C++ bindings'
        assert formats.fileFormats()[-1] == dict(name='Simple Shape', extensions=('svshape',), readable=True, writable=True)
        for args in [('Other', ['svshape']), ('USD', ['usd']), ('Simple Shape', ['other']), ('Bad', ['*.bad'])]:
            try:
                formats.registerFileFormat(*args, loader=self.plugin['load'])
            except ValueError:
                pass
            else:
                raise AssertionError('conflicting registration accepted')
        metadata = formats.fileFormats()
        metadata[-1]['name'] = 'changed'
        assert formats.fileFormats()[-1]['name'] == 'Simple Shape'
        assert 'Simple Shape (*.svshape)' in formats._filters()
        assert 'Simple Shape (*.svshape)' in formats._filters(True)
        formats.registerFileFormat('Read only', ['readtest'], loader=self.plugin['load'])
        assert '*.readtest' in formats._filters() and '*.readtest' not in formats._filters(True)
        assert formats.unregisterFileFormat(name='Read only') and not formats.unregisterFileFormat('Read only')
        formats.registerFileFormat('Write only', ['writetest'], saver=self.plugin['save'])
        assert '*.writetest' not in formats._filters() and '*.writetest' in formats._filters(True)
        formats.unregisterFileFormat('Write only')
        formats.unregisterFileFormat('Simple Shape')

        def loader(source, options):
            self.load_calls += 1
            if 'token' in options:
                assert options['token'] is self.options_token, 'binding preserves opaque Python options'
            if self.fail:
                raise RuntimeError('intentional conversion failure')
            self.provider_stage = self.plugin['load'](source, options)
            return self.provider_stage

        def options(path, operation, parent):
            self.option_calls.append(operation)
            core, _, _, widgets = tb.qt()
            dialog = widgets.QInputDialog(parent)
            dialog.setInputMode(widgets.QInputDialog.InputMode.DoubleInput)
            dialog.setLabelText('Radius scale')
            dialog.setDoubleValue(3.0)
            dialog.setObjectName('fileFormatOptionsTest')
            dialog.setWindowTitle('Simple Shape Import Settings')
            # Use a standard Qt dialog; no custom widget layout is needed here.
            def capture():
                assert dialog.isVisible()
                assert dialog.grab().save(str(self.directory / ('options_' + operation + '.png')))
                self.snapshot('format_options_' + operation)
                dialog.reject() if self.cancel else dialog.accept()
            core.QTimer.singleShot(50, capture)
            return {'scale': dialog.doubleValue(), 'token': self.options_token} if dialog.exec() == widgets.QDialog.DialogCode.Accepted else None

        formats.registerFileFormat('Simple Shape', ['svshape'], loader=loader, saver=self.plugin['save'], options=options)

    def imports(self):
        session = stageviz.session()
        assert session.newStage()
        before = session.stage().GetRootLayer().ExportToString()
        self.cancel = True
        assert not session.loadFromFile(str(self.source), interactive=True)
        assert session.stage().GetRootLayer().ExportToString() == before
        self.cancel = False
        self.fail = True
        assert not session.loadFromFile(str(self.source))
        assert session.stage().GetRootLayer().ExportToString() == before
        self.fail = False
        invalid = self.directory / 'invalid.invalidformat'
        invalid.write_text('not USD')
        for result in (None, 'legacy.usda', 42):
            stageviz.plugins.registerFileFormat('Invalid USD', ['invalidformat'],
                                         loader=lambda source, options, value=result: value)
            assert not session.loadFromFile(str(invalid))
            assert session.stage().GetRootLayer().ExportToString() == before
            try:
                stageviz.plugins.readFile(str(invalid))
            except TypeError:
                pass
            else:
                raise AssertionError('Loader must return a USD stage')
            stageviz.plugins.unregisterFileFormat('Invalid USD')
        assert not hasattr(stageviz.plugins, '_cachePath')
        assert session.loadFromFile(str(self.source))
        assert UsdGeom.Sphere(session.stage().GetPrimAtPath('/Sphere')).GetRadiusAttr().Get() == 2.0
        assert session.filename() == '' and session.stage().GetRootLayer().anonymous
        assert all(layer.anonymous for layer in session.stage().GetLayerStack())
        self.provider_stage.DefinePrim('/ProviderOnly', 'Xform')
        assert not session.stage().GetPrimAtPath('/ProviderOnly'), 'import detaches loader-owned layers'

        assert self.option_calls == ['open']
        assert session.loadFromFile(str(self.source), interactive=True)
        assert UsdGeom.Sphere(session.stage().GetPrimAtPath('/Sphere')).GetRadiusAttr().Get() == 6.0
        assert self.option_calls == ['open', 'open']
        self.snapshot('format_imported')
        saved = self.directory / 'saved.usda'
        assert session.saveToFile(str(saved))
        assert session.filename() == str(saved)
        assert self.source.read_text() == '{"radius": 2.0}'
        layer = Sdf.Layer.CreateAnonymous()
        provider = Usd.Stage.Open(layer)
        sphere = UsdGeom.Sphere.Define(provider, '/LayerSphere')
        sphere.GetRadiusAttr().Set(3.0, 1.0)
        sphere.GetRadiusAttr().Set(4.0, 2.0)
        composed = Usd.Stage.CreateInMemory()
        composed.GetRootLayer().subLayerPaths = [layer.identifier]
        composed.SetDefaultPrim(composed.GetPrimAtPath('/LayerSphere'))
        with Usd.EditContext(composed, composed.GetSessionLayer()):
            UsdGeom.Imageable(composed.GetPrimAtPath('/LayerSphere')).GetVisibilityAttr().Set('invisible')
        source = self.directory / 'source.composed'
        source.write_text('synthetic external format')
        stageviz.plugins.registerFileFormat('Composed stage', ['composed'], loader=lambda source, options: composed)
        memory = stageviz.plugins.readFile(str(source))
        assert str(memory.GetDefaultPrim().GetPath()) == '/LayerSphere'
        assert memory.GetPrimAtPath('/LayerSphere').GetAttribute('visibility').Get() == 'invisible'
        layer.Clear()
        composed.GetRootLayer().Clear()
        composed.GetSessionLayer().Clear()
        stageviz.plugins.unregisterFileFormat('Composed stage')
        del composed, provider, layer
        gc.collect()
        assert UsdGeom.Sphere(memory.GetPrimAtPath('/LayerSphere')).GetRadiusAttr().GetTimeSamples() == [1.0, 2.0]
        assert UsdGeom.Sphere(memory.GetPrimAtPath('/LayerSphere')).GetRadiusAttr().Get(2.0) == 4.0
        assert all(layer.anonymous for layer in memory.GetLayerStack())


    def merge(self):
        assert stageviz.session().newStage()
        stageviz.command.merge_stage(str(self.source))
        assert tb.wait_for_commands()
        assert stageviz.session().stage().GetPrimAtPath('/Sphere')
        calls = self.load_calls
        stageviz.command.undo()
        assert tb.wait_for_commands()
        assert not stageviz.session().stage().GetPrimAtPath('/Sphere')
        stageviz.command.redo()
        assert tb.wait_for_commands()
        assert stageviz.session().stage().GetPrimAtPath('/Sphere')
        assert self.load_calls == calls, 'redo reuses USD conversion snapshot'
        assert self.option_calls == ['open', 'open'], 'command merge never opens options'
        self.snapshot('format_merged')
        memory = stageviz.plugins.readFile(str(self.source))
        assert isinstance(memory, Usd.Stage)
        for command in (stageviz.command.merge_stage, stageviz.command.merge_flattened_stage):
            assert stageviz.session().newStage()
            command(memory)
            assert tb.wait_for_commands()
            UsdGeom.Sphere(memory.GetPrimAtPath('/Sphere')).GetRadiusAttr().Set(9.0)
            stageviz.command.undo()
            assert tb.wait_for_commands()
            stageviz.command.redo()
            assert tb.wait_for_commands()
            assert UsdGeom.Sphere(stageviz.session().stage().GetPrimAtPath('/Sphere')).GetRadiusAttr().Get() == 2.0
            UsdGeom.Sphere(memory.GetPrimAtPath('/Sphere')).GetRadiusAttr().Set(2.0)


    def exports(self):
        formats = stageviz.plugins
        assert stageviz.session().loadFromFile(str(self.source))
        usd = stageviz.session().snapshot()
        assert isinstance(usd, Usd.Stage) and usd.GetRootLayer().anonymous
        destination = self.directory / 'result.svshape'
        destination.write_text('original')
        self.cancel = True
        assert not formats.writeFile(usd, str(destination), interactive=True)
        assert destination.read_text() == 'original'
        self.cancel = False
        assert formats.writeFile(usd, str(destination))
        assert json.loads(destination.read_text()) == {'radius': 2.0}
        assert stageviz.session().filename() == ''
        assert formats.readFile(str(destination))
        formats.registerFileFormat('Broken saver', ['broken'], saver=lambda *args: (_ for _ in ()).throw(RuntimeError('expected')))
        broken = self.directory / 'result.broken'
        broken.write_text('original')
        try:
            formats.writeFile(usd, str(broken))
        except RuntimeError:
            pass
        else:
            raise AssertionError('failed saver accepted')
        assert broken.read_text() == 'original'
        assert not list(self.directory.glob('.stageviz-*'))
        formats.unregisterFileFormat('Broken saver')
        def sidecar(source, destination, options):
            Path(destination).write_text('output')
            Path(destination + '.sidecar').write_text('extra')
        formats.registerFileFormat('Sidecar saver', ['sidecar'], saver=sidecar)
        target = self.directory / 'result.sidecar'
        target.write_text('original')
        try:
            formats.writeFile(usd, str(target))
        except ValueError:
            pass
        else:
            raise AssertionError('multi-file saver accepted')
        assert target.read_text() == 'original'
        formats.unregisterFileFormat('Sidecar saver')
        def mutating(stage, destination, options):
            assert isinstance(stage, Usd.Stage) and stage.GetRootLayer().anonymous
            self.saver_stage = stage
            stage.DefinePrim('/SaverOnly', 'Xform')
            Path(destination).write_text('external output')
        formats.registerFileFormat('Mutating saver', ['mutating'], saver=mutating)
        assert formats.writeFile(usd, str(self.directory / 'output.mutating'))
        gc.collect()
        assert self.saver_stage.GetPrimAtPath('/SaverOnly'), 'retained saver stage owns its USD data'
        assert not usd.GetPrimAtPath('/SaverOnly')
        assert not stageviz.session().stage().GetPrimAtPath('/SaverOnly')
        formats.unregisterFileFormat('Mutating saver')
        for source in (str(self.directory / 'legacy.usda'), None, 42):
            try:
                formats.writeFile(source, str(destination))
            except TypeError:
                pass
            else:
                raise AssertionError('Saver source must be a USD stage')
        work = self.directory / 'memory-only'
        work.mkdir()
        source = work / 'input.svshape'
        source.write_text('{"radius": 2.0}')
        memory = formats.readFile(path=source)
        assert isinstance(memory, Usd.Stage) and memory.GetRootLayer().anonymous
        assert formats.writeFile(stage=memory, path=work / 'output.svshape')
        assert sorted(p.name for p in work.iterdir()) == ['input.svshape', 'output.svshape']
        assert stageviz.session().snapshotPaths([]) is None


    def ui(self):
        core, gui, _, widgets = tb.qt()
        core.QCoreApplication.setAttribute(core.Qt.ApplicationAttribute.AA_DontUseNativeDialogs, True)
        assert stageviz.session().saveToFile(str(self.directory / 'ui-before.usda'))

        def choose(action_name, path, writing=False):
            errors = []
            def accept_file():
                dialog = widgets.QApplication.activeModalWidget()
                try:
                    assert isinstance(dialog, widgets.QFileDialog)
                    assert 'Simple Shape (*.svshape)' in dialog.nameFilters()
                    native_filters = ' '.join(dialog.nameFilters())
                    if self.native_fixture:
                        assert ('*.svsdfruntime' not in native_filters) if writing else ('*.svsdfruntime' in native_filters)
                    dialog.setDirectory(str(path.parent))
                    dialog.selectNameFilter('Simple Shape (*.svshape)')
                    tb.process_events(150)
                    dialog.selectFile(path.name)
                    dialog.findChild(widgets.QLineEdit, 'fileNameEdit').setText(path.name)
                    assert dialog.grab().save(str(self.directory / ('dialog_' + action_name + '.png')))
                    self.snapshot('format_dialog_' + action_name)
                    dialog.findChild(widgets.QLineEdit, 'fileNameEdit').setText(path.name)
                    core.QMetaObject.invokeMethod(dialog, 'accept', core.Qt.ConnectionType.QueuedConnection)
                    def timeout():
                        try:
                            if dialog.isVisible():
                                errors.append('file dialog did not accept its selected filename')
                                dialog.reject()
                        except RuntimeError:
                            pass
                    core.QTimer.singleShot(5000, timeout)
                except Exception as error:
                    errors.append(str(error))
                    if dialog is not None:
                        dialog.reject()
            core.QTimer.singleShot(150, accept_file)
            action = tb.viewer().findChild(gui.QAction, action_name)
            assert action is not None and action.isEnabled()
            action.trigger()
            assert not errors, errors
            assert tb.wait_for_commands()

        choose('fileOpen', self.source)
        assert UsdGeom.Sphere(stageviz.session().stage().GetPrimAtPath('/Sphere')).GetRadiusAttr().Get() == 6.0
        assert stageviz.session().saveToFile(str(self.directory / 'ui-imported.usda'))
        choose('fileMergeStage', self.source)
        assert len(list(stageviz.session().stage().GetPseudoRoot().GetChildren())) == 2
        assert self.option_calls[-1] == 'merge'
        assert stageviz.session().loadFromFile(str(self.source))
        choose('fileExportAll', self.directory / 'ui-export', True)
        assert json.loads((self.directory / 'ui-export.svshape').read_text()) == {'radius': 2.0}
        assert self.option_calls[-1] == 'export'
        stageviz.command.new_prim('/', 'Other', 'Sphere')
        assert tb.wait_for_commands()
        stageviz.command.select_paths(['/Sphere'])
        assert tb.wait_for_commands()
        choose('fileExportSelected', self.directory / 'selected.svshape', True)
        assert json.loads((self.directory / 'selected.svshape').read_text()) == {'radius': 2.0}
        stageviz.command.hide_paths(['/Other'])
        assert tb.wait_for_commands()
        choose('fileExportVisible', self.directory / 'visible.svshape', True)
        assert json.loads((self.directory / 'visible.svshape').read_text()) == {'radius': 2.0}
        self.snapshot('format_ui_exported')

    def cleanup(self):
        stageviz.plugins.unregisterFileFormat('Simple Shape')
        super().cleanup()


def run(disposable=False):
    test = FileFormatsTest(disposable)
    try:
        test.setup()
        test.native_formats()
        test.registry()
        test.imports()
        test.merge()
        test.exports()
        test.ui()
    finally:
        test.cleanup()

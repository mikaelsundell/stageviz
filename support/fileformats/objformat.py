# SPDX-License-Identifier: BSD-3-Clause
"""Register Wavefront OBJ geometry conversion and its remembered options dialog."""
from pathlib import Path

from .objloader import load
from .objsaver import save

NAME = 'Wavefront OBJ'
SETTINGS_GROUP = 'fileFormats/WavefrontOBJ'


def settings():
    """Use Stageviz's existing settings store, including isolated test settings."""
    try:
        from PySide6 import QtCore
    except ImportError:
        from PyQt6 import QtCore
    return QtCore.QSettings(QtCore.QSettings.defaultFormat(), QtCore.QSettings.Scope.UserScope,
                           'com.github.mikaelsundell.stageviz', 'Stageviz')


def options(filename, operation, parent):
    """Show a modal settings callback; persist accepted values only."""
    path = Path(__file__).with_name('objoptions.ui')
    try:
        from PySide6 import QtCore, QtWidgets, QtUiTools
    except ImportError:
        from PyQt6 import QtCore, QtWidgets, uic
        dialog = uic.loadUi(str(path))
        dialog.setParent(parent, QtCore.Qt.WindowType.Dialog)
    else:
        source = QtCore.QFile(str(path))
        if not source.open(QtCore.QIODevice.OpenModeFlag.ReadOnly):
            raise OSError('Could not read OBJ options UI')
        try:
            dialog = QtUiTools.QUiLoader().load(source, parent)
        finally:
            source.close()
        if dialog is None:
            raise RuntimeError('Could not load OBJ options UI')
    mode = 'export' if operation == 'export' else 'import'
    key = SETTINGS_GROUP + '/' + mode + '/triangulate'
    preferences = settings()
    dialog.setWindowTitle('OBJ ' + mode.capitalize() + ' Options')
    dialog.findChild(QtWidgets.QLabel, 'filenameLabel').setText(Path(filename).name)
    triangulate = dialog.findChild(QtWidgets.QCheckBox, 'triangulateCheckBox')
    triangulate.setChecked(preferences.value(key, False, type=bool))
    try:
        if dialog.exec() != QtWidgets.QDialog.DialogCode.Accepted:
            return None
        result = {'triangulate': triangulate.isChecked()}
        preferences.setValue(key, result['triangulate'])
        preferences.sync()
        return result
    finally:
        dialog.deleteLater()


def register():
    """Expose OBJ in Open/Merge and Export; duplicate registration raises."""
    import stageviz
    stageviz.plugins.registerFileFormat(NAME, ['obj'], loader=load, saver=save, options=options)

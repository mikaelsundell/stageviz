# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 - present Mikael Sundell

from pathlib import Path
import traceback

from pxr import Tf
import stageviz

try:
    from PySide6 import QtCore, QtWidgets
except ImportError:
    from PyQt6 import QtCore, QtWidgets


OBJECT_NAME = 'stagevizHydraDebugDialog'
_DIALOG = None


def debug_names():
    return sorted(str(n) for n in Tf.Debug.GetDebugSymbolNames())


def is_enabled(name):
    return bool(Tf.Debug.IsDebugSymbolNameEnabled(name))


def set_enabled(name, enabled):
    # OpenUSD's Tf.Debug API supports glob-style patterns; exact names are safe.
    Tf.Debug.SetDebugSymbolsByName(name, bool(enabled))


class HydraDebugDialog(QtWidgets.QDialog):
    PREFIXES = ('HD', 'HDST', 'HGI', 'HGIGL', 'HGIMETAL', 'HGIVULKAN',
                'USDIMAGING', 'USDSKELIMAGING', 'GLF', 'HIO', 'STORM')

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName(OBJECT_NAME)
        self.setWindowTitle('Hydra Debug Settings')
        self.setWindowModality(QtCore.Qt.WindowModality.NonModal)
        self.setWindowFlag(QtCore.Qt.WindowType.Tool, True)
        self.setAttribute(QtCore.Qt.WidgetAttribute.WA_DeleteOnClose, True)
        self.resize(820, 740)
        self._updating = False
        self._names = []

        layout = QtWidgets.QVBoxLayout(self)
        layout.addWidget(QtWidgets.QLabel(
            'Enable OpenUSD debug symbols at runtime. Messages are written to the '
            'process output. Use Output file below to capture future console, USD and Qt messages.'))

        self.build_logging_controls(layout)

        controls = QtWidgets.QHBoxLayout()
        self.search = QtWidgets.QLineEdit()
        self.search.setPlaceholderText('Filter symbols or descriptions...')
        self.search.textChanged.connect(self.refresh_filter)
        controls.addWidget(self.search, 1)
        self.hydra_only = QtWidgets.QCheckBox('Hydra / imaging only')
        self.hydra_only.setChecked(True)
        self.hydra_only.toggled.connect(self.refresh_filter)
        controls.addWidget(self.hydra_only)
        layout.addLayout(controls)

        self.table = QtWidgets.QTableWidget(0, 3)
        self.table.setHorizontalHeaderLabels(['Enabled', 'Debug symbol', 'Description'])
        self.table.verticalHeader().hide()
        self.table.setSelectionBehavior(QtWidgets.QAbstractItemView.SelectionBehavior.SelectRows)
        self.table.setEditTriggers(QtWidgets.QAbstractItemView.EditTrigger.NoEditTriggers)
        self.table.horizontalHeader().setStretchLastSection(True)
        self.table.setColumnWidth(0, 75)
        self.table.setColumnWidth(1, 245)
        self.table.itemChanged.connect(self.on_item_changed)
        layout.addWidget(self.table, 1)

        presets = QtWidgets.QHBoxLayout()
        presets.addWidget(QtWidgets.QLabel('Enable preset:'))
        for title, prefixes in (
            ('Hydra', ('HD_',)),
            ('Storm', ('HDST_',)),
            ('Hgi / GPU', ('HGI_', 'HGIGL_', 'HGIMETAL_', 'HGIVULKAN_')),
            ('USD Imaging', ('USDIMAGING_',)),
        ):
            button = QtWidgets.QPushButton(title)
            button.clicked.connect(lambda checked=False, p=prefixes: self.apply_prefixes(p))
            presets.addWidget(button)
        layout.addLayout(presets)

        actions = QtWidgets.QHBoxLayout()
        for title, value in (('Enable filtered', True), ('Disable filtered', False)):
            button = QtWidgets.QPushButton(title)
            button.clicked.connect(lambda checked=False, v=value: self.apply_filtered(v))
            actions.addWidget(button)
        reset = QtWidgets.QPushButton('Disable all debug symbols')
        reset.clicked.connect(self.disable_all)
        actions.addWidget(reset)
        layout.addLayout(actions)

        footer = QtWidgets.QHBoxLayout()
        self.status = QtWidgets.QLabel()
        footer.addWidget(self.status, 1)
        refresh = QtWidgets.QPushButton('Refresh')
        refresh.clicked.connect(self.load_symbols)
        footer.addWidget(refresh)
        close = QtWidgets.QPushButton('Close')
        close.clicked.connect(self.close)
        footer.addWidget(close)
        layout.addLayout(footer)
        self.load_symbols()
        self.log_timer = QtCore.QTimer(self)
        self.log_timer.setInterval(500)
        self.log_timer.timeout.connect(self.refresh_logging)
        self.log_timer.start()

    def build_logging_controls(self, layout):
        group = QtWidgets.QGroupBox('Output file')
        group.setObjectName('hydraLogGroup')
        box = QtWidgets.QVBoxLayout(group)
        row = QtWidgets.QHBoxLayout()
        self.log_path = QtWidgets.QLineEdit()
        self.log_path.setObjectName('hydraLogPath')
        desktop = QtCore.QStandardPaths.writableLocation(QtCore.QStandardPaths.StandardLocation.DesktopLocation)
        self.log_path.setText(str(Path(desktop or str(Path.home())) / 'stageviz-hydra.log'))
        self.log_path.setPlaceholderText('Choose a log file...')
        row.addWidget(self.log_path, 1)
        self.log_browse = QtWidgets.QPushButton('Browse...')
        self.log_browse.setObjectName('hydraLogBrowse')
        self.log_browse.clicked.connect(self.browse_log)
        row.addWidget(self.log_browse)
        box.addLayout(row)
        buttons = QtWidgets.QHBoxLayout()
        self.log_append = QtWidgets.QCheckBox('Append to existing file')
        self.log_append.setObjectName('hydraLogAppend')
        self.log_append.setChecked(True)
        buttons.addWidget(self.log_append)
        buttons.addStretch(1)
        self.log_start = QtWidgets.QPushButton('Start logging')
        self.log_start.setObjectName('hydraLogStart')
        self.log_start.clicked.connect(self.start_logging)
        buttons.addWidget(self.log_start)
        self.log_stop = QtWidgets.QPushButton('Stop logging')
        self.log_stop.setObjectName('hydraLogStop')
        self.log_stop.clicked.connect(self.stop_logging)
        buttons.addWidget(self.log_stop)
        box.addLayout(buttons)
        self.log_status = QtWidgets.QLabel()
        self.log_status.setObjectName('hydraLogStatus')
        self.log_status.setWordWrap(True)
        self._log_error = ''
        box.addWidget(self.log_status)
        note = QtWidgets.QLabel('Logging continues when this dialog closes. Reopen it and press Stop logging to finish.')
        note.setWordWrap(True)
        box.addWidget(note)
        layout.addWidget(group)
        self.refresh_logging()

    def refresh_logging(self):
        active = stageviz.consoleOutputFile()
        if active:
            self.log_path.setText(active)
        self.log_path.setReadOnly(bool(active))
        self.log_browse.setEnabled(not active)
        self.log_append.setEnabled(not active)
        self.log_start.setEnabled(not active)
        self.log_stop.setEnabled(bool(active))
        self.log_status.setText(self._log_error or ('Logging to: ' + active if active else 'File logging is stopped.'))

    def browse_log(self):
        options = QtWidgets.QFileDialog.Option(0)
        if self.log_append.isChecked():
            options |= QtWidgets.QFileDialog.Option.DontConfirmOverwrite
        filename, _ = QtWidgets.QFileDialog.getSaveFileName(
            self, 'Console output file', self.log_path.text(),
            'Log files (*.log *.txt);;All files (*)', options=options)
        if filename:
            self.log_path.setText(filename)

    def start_logging(self):
        filename = self.log_path.text().strip()
        if not filename:
            self.browse_log()
            filename = self.log_path.text().strip()
        if not filename:
            return
        try:
            stageviz.setConsoleOutputFile(str(Path(filename).expanduser()), append=self.log_append.isChecked())
            self._log_error = ''
        except (OSError, ValueError) as exc:
            self._log_error = 'Could not start logging: ' + str(exc)
        self.refresh_logging()

    def stop_logging(self):
        try:
            stageviz.setConsoleOutputFile(None)
            self._log_error = ''
        except OSError as exc:
            self._log_error = 'Could not stop logging: ' + str(exc)
        self.refresh_logging()

    def description(self, name):
        try:
            return str(Tf.Debug.GetDebugSymbolDescription(name))
        except Exception:
            return ''

    def load_symbols(self):
        try:
            self._names = debug_names()
            self.refresh_filter()
        except Exception as exc:
            traceback.print_exc()
            self.status.setText(f'Error: {exc}')

    def matching(self, name):
        if self.hydra_only.isChecked() and not name.upper().startswith(self.PREFIXES):
            return False
        query = self.search.text().strip().lower()
        return not query or query in name.lower() or query in self.description(name).lower()

    def refresh_filter(self, *args):
        self._updating = True
        try:
            names = [name for name in self._names if self.matching(name)]
            self.table.setRowCount(len(names))
            for row, name in enumerate(names):
                checkbox = QtWidgets.QTableWidgetItem()
                checkbox.setFlags(QtCore.Qt.ItemFlag.ItemIsEnabled |
                                  QtCore.Qt.ItemFlag.ItemIsSelectable |
                                  QtCore.Qt.ItemFlag.ItemIsUserCheckable)
                checkbox.setCheckState(QtCore.Qt.CheckState.Checked if is_enabled(name)
                                       else QtCore.Qt.CheckState.Unchecked)
                checkbox.setData(QtCore.Qt.ItemDataRole.UserRole, name)
                self.table.setItem(row, 0, checkbox)
                self.table.setItem(row, 1, QtWidgets.QTableWidgetItem(name))
                self.table.setItem(row, 2, QtWidgets.QTableWidgetItem(self.description(name)))
            self.update_status()
        finally:
            self._updating = False

    def update_status(self):
        enabled = sum(is_enabled(n) for n in self._names)
        self.status.setText(f'{self.table.rowCount()} shown  |  {enabled} enabled  |  {len(self._names)} registered')

    def on_item_changed(self, item):
        if self._updating or item.column() != 0:
            return
        name = item.data(QtCore.Qt.ItemDataRole.UserRole)
        try:
            set_enabled(name, item.checkState() == QtCore.Qt.CheckState.Checked)
            self.update_status()
        except Exception as exc:
            traceback.print_exc()
            self.status.setText(f'Error: {exc}')
            self.refresh_filter()

    def apply_names(self, names, enabled):
        try:
            for name in names:
                set_enabled(name, enabled)
            self.refresh_filter()
        except Exception as exc:
            traceback.print_exc()
            self.status.setText(f'Error: {exc}')

    def apply_filtered(self, enabled):
        self.apply_names([n for n in self._names if self.matching(n)], enabled)

    def apply_prefixes(self, prefixes):
        self.apply_names([n for n in self._names if n.upper().startswith(prefixes)], True)

    def disable_all(self):
        self.apply_names(self._names, False)


def show_hydra_debug_dialog():
    global _DIALOG
    if _DIALOG is not None:
        try:
            _DIALOG.close()
        except RuntimeError:
            pass
        _DIALOG = None
    for window in QtWidgets.QApplication.topLevelWidgets():
        if window.objectName() == OBJECT_NAME:
            window.close()
    try:
        parent = stageviz.application().window()
    except Exception:
        parent = QtWidgets.QApplication.activeWindow()
    _DIALOG = HydraDebugDialog(parent)
    _DIALOG.show()
    _DIALOG.raise_()
    _DIALOG.activateWindow()
    return _DIALOG


hydra_debug_dialog = show_hydra_debug_dialog()

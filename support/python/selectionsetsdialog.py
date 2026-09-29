import sys
import traceback

from pxr import Sdf

import stageviz

try:
    from PySide6 import QtCore, QtWidgets
except Exception:
    from PyQt6 import QtCore, QtWidgets


DIALOG_OBJECT_NAME = "stagevizSelectionSetsDialog"
DIALOG_GLOBAL_NAME = "_stageviz_selection_sets_dialog"

ATTRIBUTE_NAME = "stageviz:selectionSets"


def find_stageviz_main_window():
    try:
        window = stageviz.Application().window()
        if window is not None:
            return window
    except Exception:
        traceback.print_exc()

    app = QtWidgets.QApplication.instance()
    if app is not None:
        return app.activeWindow()

    return None


def get_stage_and_session():
    session = stageviz.Session()
    stage = session.stage()

    if not stage:
        raise RuntimeError("No stage loaded.")

    return stage, session


def prim_has_payload(prim):
    if not prim or not prim.IsValid():
        return False

    try:
        return prim.HasPayload()
    except Exception:
        return False


def iter_assembly_prims(stage):
    """
    Traverse the assembly hierarchy.

    Payload prims are included, but nothing below a payload prim
    is traversed.
    """

    def visit(prim):
        if not prim or not prim.IsValid():
            return

        yield prim

        if prim_has_payload(prim):
            return

        for child in prim.GetChildren():
            yield from visit(child)

    root = stage.GetPseudoRoot()

    for child in root.GetChildren():
        yield from visit(child)


def get_sets_from_prim(prim):
    attr = prim.GetAttribute(ATTRIBUTE_NAME)

    if not attr or not attr.IsValid():
        return []

    value = attr.Get()

    if not value:
        return []

    return [
        str(name)
        for name in value
        if str(name)
    ]


def set_sets_on_prim(prim, names):
    names = sorted(
        set(
            str(name)
            for name in names
            if str(name)
        ),
        key=str.lower,
    )

    attr = prim.GetAttribute(
        ATTRIBUTE_NAME
    )

    if names:
        if not attr or not attr.IsValid():
            attr = prim.CreateAttribute(
                ATTRIBUTE_NAME,
                Sdf.ValueTypeNames.StringArray,
                custom=True,
            )

        attr.Set(names)
        return

    if attr and attr.IsValid():
        try:
            prim.RemoveProperty(
                ATTRIBUTE_NAME
            )
        except Exception:
            attr.Clear()


def scan_selection_sets(stage):
    sets = {}

    for prim in iter_assembly_prims(stage):
        path = str(
            prim.GetPath()
        )

        for name in get_sets_from_prim(
            prim
        ):
            sets.setdefault(
                name,
                [],
            ).append(path)

    for name in sets:
        sets[name].sort(
            key=str.lower
        )

    return sets


def add_paths_to_set(
    stage,
    paths,
    set_name,
):
    changed = 0

    for path in paths:
        prim = stage.GetPrimAtPath(
            path
        )

        if not prim or not prim.IsValid():
            continue

        names = get_sets_from_prim(
            prim
        )

        if set_name in names:
            continue

        names.append(
            set_name
        )

        set_sets_on_prim(
            prim,
            names,
        )

        changed += 1

    return changed


def remove_paths_from_set(
    stage,
    paths,
    set_name,
):
    changed = 0

    for path in paths:
        prim = stage.GetPrimAtPath(
            path
        )

        if not prim or not prim.IsValid():
            continue

        names = get_sets_from_prim(
            prim
        )

        if set_name not in names:
            continue

        names = [
            name
            for name in names
            if name != set_name
        ]

        set_sets_on_prim(
            prim,
            names,
        )

        changed += 1

    return changed


def family_prefix(set_name):
    """
    Floor_LH       -> Floor_
    Floor_RH       -> Floor_
    Door_Front_LH  -> Door_Front_

    Sets without an underscore have no toggle family.
    """

    if "_" not in set_name:
        return None

    return (
        set_name.rsplit(
            "_",
            1,
        )[0]
        + "_"
    )


class SelectionSetsDialog(
    QtWidgets.QDialog
):
    def __init__(
        self,
        parent=None,
    ):
        super().__init__(parent)

        self.setObjectName(
            DIALOG_OBJECT_NAME
        )

        self.setWindowTitle(
            "Selection Sets"
        )

        self.setWindowModality(
            QtCore.Qt.WindowModality.NonModal
        )

        self.setWindowFlag(
            QtCore.Qt.WindowType.Tool,
            True,
        )

        self.setAttribute(
            QtCore.Qt.WidgetAttribute.WA_DeleteOnClose,
            True,
        )

        self.resize(
            1080,
            480,
        )

        self._sets = {}
        self._last_selection = None

        self._build_ui()

        self._timer = QtCore.QTimer(
            self
        )

        self._timer.setInterval(
            500
        )

        self._timer.timeout.connect(
            self._live_update
        )

        self._timer.start()

        self.refresh()

    def _build_ui(self):
        layout = QtWidgets.QVBoxLayout(
            self
        )

        description = QtWidgets.QLabel(
            "Create persistent named selection sets from the "
            "current Stageviz selection. Membership is stored as "
            f"'{ATTRIBUTE_NAME}'. Sets sharing everything before "
            "their final underscore can be toggled as alternatives."
        )

        description.setWordWrap(
            True
        )

        layout.addWidget(
            description
        )

        layout.addSpacing(
            6
        )

        # ------------------------------------------------------------
        # Create
        # ------------------------------------------------------------

        create_layout = QtWidgets.QHBoxLayout()

        self.new_set_edit = QtWidgets.QLineEdit()

        self.new_set_edit.setPlaceholderText(
            "Selection set name"
        )

        self.new_set_edit.returnPressed.connect(
            self.create_set_from_selection
        )

        create_layout.addWidget(
            self.new_set_edit,
            1,
        )

        create_button = QtWidgets.QPushButton(
            "Create from Selection"
        )

        create_button.clicked.connect(
            self.create_set_from_selection
        )

        create_layout.addWidget(
            create_button
        )

        layout.addLayout(
            create_layout
        )

        # ------------------------------------------------------------
        # Table
        # ------------------------------------------------------------

        self.table = QtWidgets.QTableWidget(
            0,
            5,
            self,
        )

        self.table.setHorizontalHeaderLabels(
            [
                "Selection Set",
                "Members",
                "Selection",
                "Visibility",
                "Set",
            ]
        )

        self.table.verticalHeader().setVisible(
            False
        )

        self.table.setSelectionBehavior(
            QtWidgets.QAbstractItemView.SelectionBehavior.SelectRows
        )

        self.table.setSelectionMode(
            QtWidgets.QAbstractItemView.SelectionMode.SingleSelection
        )

        self.table.setEditTriggers(
            QtWidgets.QAbstractItemView.EditTrigger.NoEditTriggers
        )

        self.table.setAlternatingRowColors(
            True
        )

        header = self.table.horizontalHeader()

        header.setSectionResizeMode(
            0,
            QtWidgets.QHeaderView.ResizeMode.Stretch,
        )

        header.setSectionResizeMode(
            1,
            QtWidgets.QHeaderView.ResizeMode.ResizeToContents,
        )

        header.setSectionResizeMode(
            2,
            QtWidgets.QHeaderView.ResizeMode.ResizeToContents,
        )

        header.setSectionResizeMode(
            3,
            QtWidgets.QHeaderView.ResizeMode.ResizeToContents,
        )

        header.setSectionResizeMode(
            4,
            QtWidgets.QHeaderView.ResizeMode.ResizeToContents,
        )

        self.table.cellDoubleClicked.connect(
            self._table_double_clicked
        )

        layout.addWidget(
            self.table,
            1,
        )

        # ------------------------------------------------------------
        # Current set actions
        # ------------------------------------------------------------

        actions_layout = QtWidgets.QHBoxLayout()

        add_button = QtWidgets.QPushButton(
            "Add Selected"
        )

        add_button.clicked.connect(
            self.add_selected_to_current_set
        )

        actions_layout.addWidget(
            add_button
        )

        remove_button = QtWidgets.QPushButton(
            "Remove Selected"
        )

        remove_button.clicked.connect(
            self.remove_selected_from_current_set
        )

        actions_layout.addWidget(
            remove_button
        )

        delete_button = QtWidgets.QPushButton(
            "Delete Set"
        )

        delete_button.clicked.connect(
            self.delete_current_set
        )

        actions_layout.addWidget(
            delete_button
        )

        actions_layout.addStretch(
            1
        )

        self.live_check = QtWidgets.QCheckBox(
            "Live"
        )

        self.live_check.setChecked(
            True
        )

        actions_layout.addWidget(
            self.live_check
        )

        refresh_button = QtWidgets.QPushButton(
            "Refresh"
        )

        refresh_button.clicked.connect(
            self.refresh
        )

        actions_layout.addWidget(
            refresh_button
        )

        layout.addLayout(
            actions_layout
        )

        # ------------------------------------------------------------
        # Status
        # ------------------------------------------------------------

        bottom_layout = QtWidgets.QHBoxLayout()

        self.selection_label = QtWidgets.QLabel(
            "Selection: 0"
        )

        bottom_layout.addWidget(
            self.selection_label
        )

        bottom_layout.addSpacing(
            16
        )

        self.status_label = QtWidgets.QLabel()

        bottom_layout.addWidget(
            self.status_label,
            1,
        )

        close_button = QtWidgets.QPushButton(
            "Close"
        )

        close_button.clicked.connect(
            self.close
        )

        bottom_layout.addWidget(
            close_button
        )

        layout.addLayout(
            bottom_layout
        )

    # ------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------

    def _selected_set_name(self):
        row = self.table.currentRow()

        if row < 0:
            return None

        item = self.table.item(
            row,
            0,
        )

        if item is None:
            return None

        return item.data(
            QtCore.Qt.ItemDataRole.UserRole
        )

    def _restore_current_set(
        self,
        set_name,
    ):
        if not set_name:
            return

        for row in range(
            self.table.rowCount()
        ):
            item = self.table.item(
                row,
                0,
            )

            if item is None:
                continue

            name = item.data(
                QtCore.Qt.ItemDataRole.UserRole
            )

            if name == set_name:
                self.table.setCurrentCell(
                    row,
                    0,
                )

                self.table.selectRow(
                    row
                )

                return

    def _family_sets(
        self,
        set_name,
    ):
        prefix = family_prefix(
            set_name
        )

        if not prefix:
            return []

        return [
            name
            for name in self._sets.keys()
            if name.startswith(prefix)
        ]

    # ------------------------------------------------------------
    # Refresh
    # ------------------------------------------------------------

    def refresh(self):
        try:
            stage, session = (
                get_stage_and_session()
            )

            current_set = (
                self._selected_set_name()
            )

            self._sets = scan_selection_sets(
                stage
            )

            self._rebuild_table()

            self._restore_current_set(
                current_set
            )

            selection = tuple(
                str(path)
                for path in session.paths()
            )

            self._last_selection = (
                selection
            )

            self.selection_label.setText(
                f"Selection: {len(selection)}"
            )

            count = len(
                self._sets
            )

            self.status_label.setText(
                f"{count} selection set"
                f"{'' if count == 1 else 's'}."
            )

        except Exception as exc:
            self._show_error(
                exc
            )

    def _rebuild_table(self):
        self.table.setRowCount(
            0
        )

        for set_name in sorted(
            self._sets.keys(),
            key=str.lower,
        ):
            paths = self._sets[
                set_name
            ]

            row = self.table.rowCount()

            self.table.insertRow(
                row
            )

            # --------------------------------------------------------
            # Name
            # --------------------------------------------------------

            name_item = QtWidgets.QTableWidgetItem(
                set_name
            )

            name_item.setData(
                QtCore.Qt.ItemDataRole.UserRole,
                set_name,
            )

            name_item.setToolTip(
                "\n".join(paths)
            )

            self.table.setItem(
                row,
                0,
                name_item,
            )

            # --------------------------------------------------------
            # Members
            # --------------------------------------------------------

            count_item = QtWidgets.QTableWidgetItem(
                str(len(paths))
            )

            count_item.setTextAlignment(
                int(
                    QtCore.Qt.AlignmentFlag.AlignCenter
                )
            )

            self.table.setItem(
                row,
                1,
                count_item,
            )

            # --------------------------------------------------------
            # Selection
            # --------------------------------------------------------

            selection_widget = QtWidgets.QWidget()

            selection_layout = QtWidgets.QHBoxLayout(
                selection_widget
            )

            selection_layout.setContentsMargins(
                2,
                2,
                2,
                2,
            )

            selection_layout.setSpacing(
                4
            )

            select_button = QtWidgets.QPushButton(
                "Select"
            )

            select_button.clicked.connect(
                lambda checked=False, name=set_name:
                self.select_set(
                    name
                )
            )

            selection_layout.addWidget(
                select_button
            )

            add_button = QtWidgets.QPushButton(
                "+"
            )

            add_button.setFixedWidth(
                28
            )

            add_button.setToolTip(
                "Add current Stageviz selection to this set"
            )

            add_button.clicked.connect(
                lambda checked=False, name=set_name:
                self.add_selected_to_set(
                    name
                )
            )

            selection_layout.addWidget(
                add_button
            )

            remove_button = QtWidgets.QPushButton(
                "-"
            )

            remove_button.setFixedWidth(
                28
            )

            remove_button.setToolTip(
                "Remove current Stageviz selection from this set"
            )

            remove_button.clicked.connect(
                lambda checked=False, name=set_name:
                self.remove_selected_from_set(
                    name
                )
            )

            selection_layout.addWidget(
                remove_button
            )

            self.table.setCellWidget(
                row,
                2,
                selection_widget,
            )

            # --------------------------------------------------------
            # Visibility
            # --------------------------------------------------------

            visibility_widget = QtWidgets.QWidget()

            visibility_layout = QtWidgets.QHBoxLayout(
                visibility_widget
            )

            visibility_layout.setContentsMargins(
                2,
                2,
                2,
                2,
            )

            visibility_layout.setSpacing(
                4
            )

            show_button = QtWidgets.QPushButton(
                "Show"
            )

            show_button.clicked.connect(
                lambda checked=False, name=set_name:
                self.show_set(
                    name
                )
            )

            visibility_layout.addWidget(
                show_button
            )

            hide_button = QtWidgets.QPushButton(
                "Hide"
            )

            hide_button.clicked.connect(
                lambda checked=False, name=set_name:
                self.hide_set(
                    name
                )
            )

            visibility_layout.addWidget(
                hide_button
            )

            self.table.setCellWidget(
                row,
                3,
                visibility_widget,
            )

            # --------------------------------------------------------
            # Toggle set
            # --------------------------------------------------------

            toggle_widget = QtWidgets.QWidget()

            toggle_layout = QtWidgets.QHBoxLayout(
                toggle_widget
            )

            toggle_layout.setContentsMargins(
                2,
                2,
                2,
                2,
            )

            toggle_layout.setSpacing(
                4
            )

            toggle_button = QtWidgets.QPushButton(
                "Toggle Set"
            )

            family_sets = self._family_sets(
                set_name
            )

            prefix = family_prefix(
                set_name
            )

            toggle_button.setEnabled(
                prefix is not None
                and len(family_sets) > 1
            )

            if prefix:
                toggle_button.setToolTip(
                    f"Show '{set_name}' and hide the other "
                    f"sets matching {prefix}*"
                )
            else:
                toggle_button.setToolTip(
                    "Set name needs an underscore to form a toggle group"
                )

            toggle_button.clicked.connect(
                lambda checked=False, name=set_name:
                self.toggle_set(
                    name
                )
            )

            toggle_layout.addWidget(
                toggle_button
            )

            self.table.setCellWidget(
                row,
                4,
                toggle_widget,
            )

        self.table.resizeRowsToContents()

    # ------------------------------------------------------------
    # Live
    # ------------------------------------------------------------

    def _live_update(self):
        if not self.live_check.isChecked():
            return

        try:
            session = stageviz.Session()

            if not session.isLoaded():
                return

            selection = tuple(
                str(path)
                for path in session.paths()
            )

            if selection == self._last_selection:
                return

            self._last_selection = (
                selection
            )

            self.selection_label.setText(
                f"Selection: {len(selection)}"
            )

        except Exception:
            pass

    # ------------------------------------------------------------
    # Create
    # ------------------------------------------------------------

    def create_set_from_selection(self):
        try:
            set_name = (
                self.new_set_edit.text().strip()
            )

            if not set_name:
                self.status_label.setText(
                    "Enter a selection set name."
                )
                return

            stage, session = (
                get_stage_and_session()
            )

            paths = [
                str(path)
                for path in session.paths()
            ]

            if not paths:
                self.status_label.setText(
                    "Select one or more prims first."
                )
                return

            changed = add_paths_to_set(
                stage,
                paths,
                set_name,
            )

            self.new_set_edit.clear()

            self.refresh()

            self._restore_current_set(
                set_name
            )

            self.status_label.setText(
                f"Added {changed} prim"
                f"{'' if changed == 1 else 's'} "
                f"to '{set_name}'."
            )

        except Exception as exc:
            self._show_error(
                exc
            )

    # ------------------------------------------------------------
    # Membership
    # ------------------------------------------------------------

    def add_selected_to_current_set(self):
        set_name = (
            self._selected_set_name()
        )

        if not set_name:
            self.status_label.setText(
                "Select a selection-set row first."
            )
            return

        self.add_selected_to_set(
            set_name
        )

    def remove_selected_from_current_set(self):
        set_name = (
            self._selected_set_name()
        )

        if not set_name:
            self.status_label.setText(
                "Select a selection-set row first."
            )
            return

        self.remove_selected_from_set(
            set_name
        )

    def add_selected_to_set(
        self,
        set_name,
    ):
        try:
            stage, session = (
                get_stage_and_session()
            )

            paths = [
                str(path)
                for path in session.paths()
            ]

            if not paths:
                self.status_label.setText(
                    "Nothing selected."
                )
                return

            changed = add_paths_to_set(
                stage,
                paths,
                set_name,
            )

            self.refresh()

            self._restore_current_set(
                set_name
            )

            self.status_label.setText(
                f"Added {changed} prim"
                f"{'' if changed == 1 else 's'} "
                f"to '{set_name}'."
            )

        except Exception as exc:
            self._show_error(
                exc
            )

    def remove_selected_from_set(
        self,
        set_name,
    ):
        try:
            stage, session = (
                get_stage_and_session()
            )

            paths = [
                str(path)
                for path in session.paths()
            ]

            if not paths:
                self.status_label.setText(
                    "Nothing selected."
                )
                return

            changed = remove_paths_from_set(
                stage,
                paths,
                set_name,
            )

            self.refresh()

            self._restore_current_set(
                set_name
            )

            self.status_label.setText(
                f"Removed {changed} prim"
                f"{'' if changed == 1 else 's'} "
                f"from '{set_name}'."
            )

        except Exception as exc:
            self._show_error(
                exc
            )

    def delete_current_set(self):
        try:
            set_name = (
                self._selected_set_name()
            )

            if not set_name:
                self.status_label.setText(
                    "Select a selection-set row first."
                )
                return

            stage, session = (
                get_stage_and_session()
            )

            paths = list(
                self._sets.get(
                    set_name,
                    [],
                )
            )

            if not paths:
                return

            removed = remove_paths_from_set(
                stage,
                paths,
                set_name,
            )

            self.refresh()

            self.status_label.setText(
                f"Deleted '{set_name}' from "
                f"{removed} prim"
                f"{'' if removed == 1 else 's'}."
            )

        except Exception as exc:
            self._show_error(
                exc
            )

    # ------------------------------------------------------------
    # Selection
    # ------------------------------------------------------------

    def select_set(
        self,
        set_name,
    ):
        try:
            paths = self._sets.get(
                set_name,
                [],
            )

            if not paths:
                return

            stageviz.command.select_paths(
                paths
            )

            self.status_label.setText(
                f"Selected {len(paths)} member"
                f"{'' if len(paths) == 1 else 's'} "
                f"of '{set_name}'."
            )

        except Exception as exc:
            self._show_error(
                exc
            )

    def _table_double_clicked(
        self,
        row,
        column,
    ):
        item = self.table.item(
            row,
            0,
        )

        if item is None:
            return

        set_name = item.data(
            QtCore.Qt.ItemDataRole.UserRole
        )

        if set_name:
            self.select_set(
                set_name
            )

    # ------------------------------------------------------------
    # Visibility
    # ------------------------------------------------------------

    def show_set(
        self,
        set_name,
    ):
        try:
            paths = self._sets.get(
                set_name,
                [],
            )

            if not paths:
                return

            stageviz.command.show_paths(
                paths,
                recursive=False,
            )

            self.status_label.setText(
                f"Shown '{set_name}'."
            )

        except Exception as exc:
            self._show_error(
                exc
            )

    def hide_set(
        self,
        set_name,
    ):
        try:
            paths = self._sets.get(
                set_name,
                [],
            )

            if not paths:
                return

            stageviz.command.hide_paths(
                paths,
                recursive=False,
            )

            self.status_label.setText(
                f"Hidden '{set_name}'."
            )

        except Exception as exc:
            self._show_error(
                exc
            )

    # ------------------------------------------------------------
    # Toggle set
    # ------------------------------------------------------------

    def toggle_set(
        self,
        set_name,
    ):
        try:
            prefix = family_prefix(
                set_name
            )

            if not prefix:
                self.status_label.setText(
                    f"'{set_name}' has no toggle group."
                )
                return

            family_sets = self._family_sets(
                set_name
            )

            if len(family_sets) < 2:
                self.status_label.setText(
                    f"No other sets matching '{prefix}*'."
                )
                return

            current_paths = list(
                self._sets.get(
                    set_name,
                    [],
                )
            )

            current_path_set = set(
                current_paths
            )

            other_paths = []
            seen = set()

            for family_name in family_sets:
                if family_name == set_name:
                    continue

                for path in self._sets.get(
                    family_name,
                    [],
                ):
                    if path in current_path_set:
                        continue

                    if path in seen:
                        continue

                    seen.add(
                        path
                    )

                    other_paths.append(
                        path
                    )

            # Hide all alternatives first.
            if other_paths:
                stageviz.command.hide_paths(
                    other_paths,
                    recursive=False,
                )

            # Then explicitly show the selected set.
            # This ensures shared members remain visible.
            if current_paths:
                stageviz.command.show_paths(
                    current_paths,
                    recursive=False,
                )

            self.status_label.setText(
                f"Activated '{set_name}' and hid "
                f"{len(family_sets) - 1} other "
                f"'{prefix}*' set"
                f"{'' if len(family_sets) == 2 else 's'}."
            )

        except Exception as exc:
            self._show_error(
                exc
            )

    # ------------------------------------------------------------
    # Errors
    # ------------------------------------------------------------

    def _show_error(
        self,
        exc,
    ):
        self.status_label.setText(
            f"Error: {type(exc).__name__}: {exc}"
        )

        traceback.print_exc()


def show_selection_sets_dialog():
    app = QtWidgets.QApplication.instance()
    owns_app = False

    if app is None:
        app = QtWidgets.QApplication(
            sys.argv
        )

        owns_app = True

    old_window = globals().get(
        DIALOG_GLOBAL_NAME
    )

    if old_window is not None:
        try:
            old_window.close()
            old_window.deleteLater()
        except Exception:
            pass

        globals()[
            DIALOG_GLOBAL_NAME
        ] = None

    parent = (
        find_stageviz_main_window()
    )

    window = SelectionSetsDialog(
        parent
    )

    window.show()
    window.raise_()
    window.activateWindow()

    globals()[
        DIALOG_GLOBAL_NAME
    ] = window

    if owns_app:
        app.exec()

    return window


selection_sets_dialog = (
    show_selection_sets_dialog()
)
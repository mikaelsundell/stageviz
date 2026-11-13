# SPDX-License-Identifier: BSD-3-Clause

add_test(
    NAME stageviz.python_documentation
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/check_python_documentation.py"
)
add_test(
    NAME stageviz.python_documentation_guard
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/test_python_documentation.py"
)
add_test(
    NAME stageviz.test_app_runner
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/test_run_all.py"
)
set_tests_properties(stageviz.test_app_runner PROPERTIES LABELS "python;tooling" TIMEOUT 30)
add_test(
    NAME stageviz.clang_format_scope
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/test_clang_format.py"
)
set_tests_properties(stageviz.clang_format_scope PROPERTIES LABELS "python;tooling" TIMEOUT 30)
set_tests_properties(stageviz.python_documentation stageviz.python_documentation_guard PROPERTIES
    LABELS "python;documentation"
    TIMEOUT 30
)

# Reuse the application's sources and dependency settings so Session and its
# collaborators are tested without replacing them with test doubles. This
# executable has a QCoreApplication entry point and does not initialize Python.
source_group(
    TREE "${CMAKE_CURRENT_SOURCE_DIR}"
    PREFIX "Test Files"
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/regression.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/gui_regression.cpp"
)

get_target_property(stageviz_test_sources ${project_name} SOURCES)
list(FILTER stageviz_test_sources EXCLUDE REGEX "(^|/)(main|test|regression|gui_regression)\\.cpp$")
list(FILTER stageviz_test_sources EXCLUDE REGEX "\\.rc$")
set(stageviz_test_absolute_sources)
foreach(source IN LISTS stageviz_test_sources)
    if(IS_ABSOLUTE "${source}")
        list(APPEND stageviz_test_absolute_sources "${source}")
    else()
        list(APPEND stageviz_test_absolute_sources "${PROJECT_SOURCE_DIR}/${source}")
    endif()
endforeach()

add_executable(stageviz_regressions regression.cpp ${stageviz_test_absolute_sources})
target_include_directories(stageviz_regressions PRIVATE
    "${PROJECT_SOURCE_DIR}/sources"
    "$<TARGET_PROPERTY:${project_name},INCLUDE_DIRECTORIES>"
)
target_compile_definitions(stageviz_regressions PRIVATE
    "$<TARGET_PROPERTY:${project_name},COMPILE_DEFINITIONS>"
)
target_link_libraries(stageviz_regressions PRIVATE
    "$<TARGET_PROPERTY:${project_name},LINK_LIBRARIES>"
)
set_target_properties(stageviz_regressions PROPERTIES
    AUTOUIC_SEARCH_PATHS "${project_source_dirs}"
)

foreach(case IN ITEMS
    perf_scope_unset
    perf_scope_empty
    perf_scope_enabled
    render_scene_index_removal
    save_twice_usda
    save_twice_usdc
    save_failure
    console_output_file_api
    viewport_color_query_api
    sublayer_save
    save_as
    save_as_payload_anchor
    corrupt_state
    payload_state
    legacy_payload_state
    reparent_dependencies
    rename_dependencies
    reparent_collision
    reparent_into_descendant
    reparent_batch
    namespace_load_rules
    namespace_load_rules_batch
    namespace_load_rules_batch_failure
    namespace_load_rules_rename
    export_session_opinions
    export_subset_metadata
    export_instances
    export_multiple_session_opinions
    merge_asset_paths
    merge_composition_asset_paths
    selection_list_api
    selection_list_semantics
    selection_list_large
    view_camera_api
    view_state_api
    session_core_api
    session_file_api
    session_layer_colors_api
    session_edit_layer_api
    session_layer_commands
    session_layer_batch_api
    session_layer_update_api
    session_state_api
    session_layer_state_api
    session_state_dirty_api
    session_load_policy_api
    command_merge_api
    namespace_editor_crud_api
    usd_path_utils_api
    transform_instance_targets
    transform_source_child_api
    indexed_path_api
    qt_path_hash_api
    spec_deletion_api
    batched_attribute_authoring
    bbox_cache_invalidation
    batched_transform_authoring
    usd_stage_utils_api
    usd_payload_utils_api
    material_utils_api
    geometry_subset_api
    material_menu_api
    command_material_api
    reset_opinion_groups_api
    make_editable_instance_api
    command_selection_api
    command_authoring_api
    new_xform_grouping_api
    command_generated_geometry_api
    command_property_api
    command_attribute_lifecycle_api
    command_pivot_lifecycle_api
    command_center_pivot_object_space
    command_visibility_stage_api
    command_material_variant_api
    command_composition_api
    command_move_api
    light_command_api
    render_resources_api
    copy_transform_api
    transform_operation_api
    pivot_operation_api
    command_payload_api
    payload_nested_rules
    payload_overlapping_batch
)
    add_test(NAME stageviz.${case} COMMAND $<TARGET_FILE:stageviz_regressions> ${case})
    set_tests_properties(stageviz.${case} PROPERTIES LABELS "native;regression" TIMEOUT 60)
endforeach()

# GUI/offscreen regressions use QApplication because Qt requires QWindow-backed
# offscreen surfaces and OpenGL resources to be created on the GUI thread.
add_executable(stageviz_gui_regressions gui_regression.cpp ${stageviz_test_absolute_sources})
foreach(test_target IN ITEMS stageviz_regressions stageviz_gui_regressions)
    add_custom_command(TARGET ${test_target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${test_target}>/resources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/resources/Render.glslfx"
            "${PROJECT_SOURCE_DIR}/resources/SceneId.glslfx"
            "$<TARGET_FILE_DIR:${test_target}>/resources"
        VERBATIM
    )
endforeach()
target_include_directories(stageviz_gui_regressions PRIVATE
    "${PROJECT_SOURCE_DIR}/sources"
    "$<TARGET_PROPERTY:${project_name},INCLUDE_DIRECTORIES>"
)
target_compile_definitions(stageviz_gui_regressions PRIVATE
    "$<TARGET_PROPERTY:${project_name},COMPILE_DEFINITIONS>"
)
target_link_libraries(stageviz_gui_regressions PRIVATE
    "$<TARGET_PROPERTY:${project_name},LINK_LIBRARIES>"
)
set_target_properties(stageviz_gui_regressions PROPERTIES
    AUTOUIC_SEARCH_PATHS "${project_source_dirs}"
)

foreach(case IN ITEMS
    viewport_components_api
    mcp_server_api
    viewport_capture_paths_api
    outliner_layer_mute_api
    viewport_lights_api
    viewport_transform_pivot_api
    viewport_transform_hierarchy_sync_api
    viewport_transform_individual_rotation_api
    viewport_gizmo_default_frame_api
    viewport_gizmo_clipping_api
    viewport_transform_selection_refresh_api
    transform_operation_applied_api
    pivot_operation_applied_api
    settings_api
    treewidget_selection_api
    indextree_selection_api
    indextree_selection_modes_api
    ui_api
    numeric_editor_api
    renderengine_api
    render_primitive_selection_api
    renderengine_hdri_api
    material_browser_api
    property_reveal_file_api
    material_numeric_editor_api
    property_asset_editor_api
    property_material_light_dropdown_api
    material_swatch_alpha_api
    geometry_subset_gui_api
    outliner_reset_opinions_menu_api
    outliner_layer_colors_api
    outliner_edit_layer_font_api
    outliner_defined_prims_api
    outliner_empty_group_checkbox_api
    outliner_instance_payload_resync_api
    outliner_payload_group_checkbox_api
    outliner_payload_queued_checkbox_api
    changes_dialog_api
    preserve_state_menu_api
    layer_tree_root_row_api
    layer_tree_multi_selection_api
)
    if(APPLE)
        # Qt debug frameworks on macOS use the _debug image suffix. The GUI
        # regression executable links against Qt*_debug, and the Cocoa platform
        # plugin must resolve against the same debug Qt binaries. Set the suffix
        # before the test process starts so dyld never mixes release and debug
        # Qt frameworks in one process. In non-Debug configurations the suffix is
        # explicitly empty, which preserves the normal release lookup behavior.
        add_test(
            NAME stageviz.gui.${case}
            COMMAND ${CMAKE_COMMAND} -E env
                "DYLD_IMAGE_SUFFIX=_debug"
                $<TARGET_FILE:stageviz_gui_regressions>
                ${case}
        )
    else()
        add_test(
            NAME stageviz.gui.${case}
            COMMAND $<TARGET_FILE:stageviz_gui_regressions> ${case}
        )
    endif()

    set_tests_properties(stageviz.gui.${case} PROPERTIES
        LABELS "native;gui;regression"
        TIMEOUT 120
    )
endforeach()

# This case drives popup actions with synthetic Qt key events. Native desktop
# focus changes can dismiss the menu before its inspection timer runs. Keep
# the C++ selection/command assertions deterministic without relying on OS focus.
set_property(TEST stageviz.gui.layer_tree_multi_selection_api APPEND PROPERTY
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Convenience target that builds every native Stageviz regression executable.
# This intentionally builds the executables only; CTest remains responsible
# for executing the individual registered test cases.
add_custom_target(stageviz_tests)
add_dependencies(stageviz_tests
    stageviz_regressions
    stageviz_gui_regressions
)

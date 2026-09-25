# SPDX-License-Identifier: BSD-3-Clause

# Reuse the application's sources and dependency settings so Session and its
# collaborators are tested without replacing them with test doubles. This
# executable has a QCoreApplication entry point and does not initialize Python.
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
    AUTOUIC_SEARCH_PATHS "${PROJECT_SOURCE_DIR}/sources"
)

foreach(case IN ITEMS
    save_twice_usda
    save_twice_usdc
    save_failure
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
    export_multiple_session_opinions
    merge_asset_paths
    merge_composition_asset_paths
    selection_list_api
    view_camera_api
    view_state_api
    session_core_api
    session_file_api
    session_edit_layer_api
    session_state_api
    session_load_policy_api
    command_merge_api
    namespace_editor_crud_api
    usd_path_utils_api
    usd_stage_utils_api
    usd_payload_utils_api
    material_utils_api
    material_menu_api
    command_material_api
    command_selection_api
    command_authoring_api
    command_property_api
    command_visibility_stage_api
    command_material_variant_api
    command_composition_api
    command_move_api
    command_payload_api
)
    add_test(NAME stageviz.${case} COMMAND $<TARGET_FILE:stageviz_regressions> ${case})
    set_tests_properties(stageviz.${case} PROPERTIES LABELS "native;regression" TIMEOUT 60)
endforeach()

# GUI/offscreen regressions use QApplication because Qt requires QWindow-backed
# offscreen surfaces and OpenGL resources to be created on the GUI thread.
add_executable(stageviz_gui_regressions gui_regression.cpp ${stageviz_test_absolute_sources})
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
    AUTOUIC_SEARCH_PATHS "${PROJECT_SOURCE_DIR}/sources"
)

foreach(case IN ITEMS settings_api style_api renderengine_api material_browser_api)
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
                "DYLD_IMAGE_SUFFIX=$<$<CONFIG:Debug>:_debug>"
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

# Convenience target that builds every native Stageviz regression executable.
# This intentionally builds the executables only; CTest remains responsible
# for executing the individual registered test cases.
add_custom_target(stageviz_tests)
add_dependencies(stageviz_tests
    stageviz_regressions
    stageviz_gui_regressions
)

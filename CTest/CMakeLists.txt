# SPDX-License-Identifier: BSD-3-Clause

# Reuse the application's sources and dependency settings so Session and its
# collaborators are tested without replacing them with test doubles. This
# executable has a QCoreApplication entry point and does not initialize Python.
get_target_property(stageviz_test_sources ${project_name} SOURCES)
list(FILTER stageviz_test_sources EXCLUDE REGEX "(^|/)(main|test)\\.cpp$")
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

foreach(case IN ITEMS save_twice save_failure sublayer_save save_as
                      corrupt_state payload_state legacy_payload_state
                      reparent_dependencies reparent_batch namespace_load_rules export_session_opinions
                      merge_asset_paths)
    add_test(NAME stageviz.${case} COMMAND stageviz_regressions ${case})
    set_tests_properties(stageviz.${case} PROPERTIES LABELS "native;regression" TIMEOUT 60)
endforeach()

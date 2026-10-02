add_executable(tst_databasesession tst_databasesession.cpp)
target_link_libraries(tst_databasesession PRIVATE Qt::Test studiocore)
add_test(NAME databasesession COMMAND tst_databasesession)

add_executable(tst_rowsmodel tst_rowsmodel.cpp)
target_link_libraries(tst_rowsmodel PRIVATE Qt::Test studiocore)
add_test(NAME rowsmodel COMMAND tst_rowsmodel)

add_executable(tst_erlayout tst_erlayout.cpp)
target_link_libraries(tst_erlayout PRIVATE Qt::Test studiocore)
add_test(NAME erlayout COMMAND tst_erlayout)

add_executable(tst_querymodel tst_querymodel.cpp)
target_link_libraries(tst_querymodel PRIVATE Qt::Test studiocore)
add_test(NAME querymodel COMMAND tst_querymodel)

add_executable(tst_codegen tst_codegen.cpp)
target_link_libraries(tst_codegen PRIVATE Qt::Test studiocore)
add_test(NAME codegen COMMAND tst_codegen)

# Exports the sample as a project, then really configures, builds and tests it
# (skips without cmake). Slow: it compiles Qivot.
add_executable(tst_projectexport tst_projectexport.cpp)
target_link_libraries(tst_projectexport PRIVATE Qt::Test studiocore)
add_test(NAME projectexport COMMAND tst_projectexport)
set_tests_properties(projectexport PROPERTIES TIMEOUT 600)

add_executable(tst_ide tst_ide.cpp)
target_link_libraries(tst_ide PRIVATE Qt::Test studiocore)
add_test(NAME ide COMMAND tst_ide)

add_executable(tst_design tst_design.cpp)
target_link_libraries(tst_design PRIVATE Qt::Test studiocore)
add_test(NAME design COMMAND tst_design)

add_executable(tst_querybuilder tst_querybuilder.cpp)
target_link_libraries(tst_querybuilder PRIVATE Qt::Test studiocore)
add_test(NAME querybuilder COMMAND tst_querybuilder)

add_executable(tst_profile tst_profile.cpp)
target_link_libraries(tst_profile PRIVATE Qt::Test studiocore)
add_test(NAME profile COMMAND tst_profile)

add_executable(tst_datatransfer tst_datatransfer.cpp)
target_link_libraries(tst_datatransfer PRIVATE Qt::Test studiocore)
add_test(NAME datatransfer COMMAND tst_datatransfer)

add_executable(tst_compare tst_compare.cpp)
target_link_libraries(tst_compare PRIVATE Qt::Test studiocore)
add_test(NAME compare COMMAND tst_compare)

add_executable(tst_diagramexport tst_diagramexport.cpp)
target_link_libraries(tst_diagramexport PRIVATE Qt::Test studiocore)
add_test(NAME diagramexport COMMAND tst_diagramexport)
set_tests_properties(diagramexport PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_samples tst_samples.cpp)
target_link_libraries(tst_samples PRIVATE Qt::Test studiocore)
add_test(NAME samples COMMAND tst_samples)

# Live servers with the public sample databases; each test skips unless its
# STUDIO_TEST_* variable is set (see the top of tst_servers.cpp).
add_executable(tst_servers tst_servers.cpp)
target_link_libraries(tst_servers PRIVATE Qt::Test studiocore)
add_test(NAME servers COMMAND tst_servers)

# The app smoke tests run offscreen with Qt Quick's software renderer: no GPU
# or display needed (Qt 5's offscreen platform has no OpenGL on Linux, and
# Qt Quick 5 aborts without it).
set(STUDIO_SMOKE_ENV "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

# The app itself must load with no QML warnings (offscreen, so CI needs no display).
add_test(NAME app_smoke COMMAND qivot-studio --sample --smoke)
set_tests_properties(app_smoke PROPERTIES ENVIRONMENT "${STUDIO_SMOKE_ENV}")

# Same, on the Data tab of the biggest table (exercises the grid and paging).
add_test(NAME app_smoke_data COMMAND qivot-studio --sample --table order_item --view data --smoke)
set_tests_properties(app_smoke_data PROPERTIES ENVIRONMENT "${STUDIO_SMOKE_ENV}")

# The other samples open (and the welcome screen lists them all).
add_test(NAME app_smoke_samples COMMAND qivot-studio --open-sample university --view samples --smoke)
set_tests_properties(app_smoke_samples PROPERTIES ENVIRONMENT "${STUDIO_SMOKE_ENV}")
add_test(NAME app_smoke_samples_page COMMAND qivot-studio --view samples --smoke)
set_tests_properties(app_smoke_samples_page PROPERTIES ENVIRONMENT "${STUDIO_SMOKE_ENV}")
# Scrolling the diagram: a wheel zooms, then a trackpad pans.
add_test(NAME app_smoke_wheel COMMAND qivot-studio --open-sample music --wheel 3 --wheel-pixels -120,-200 --smoke)
set_tests_properties(app_smoke_wheel PROPERTIES ENVIRONMENT "${STUDIO_SMOKE_ENV}")
# The new ways to change things: editing rows (with unsaved edits), and Compare.
add_test(NAME app_smoke_edit COMMAND qivot-studio --sample --table publisher --view data --allow-changes --edit-demo --smoke)
set_tests_properties(app_smoke_edit PROPERTIES ENVIRONMENT "${STUDIO_SMOKE_ENV}")
add_test(NAME app_smoke_compare COMMAND qivot-studio --sample --view compare --smoke)
set_tests_properties(app_smoke_compare PROPERTIES ENVIRONMENT "${STUDIO_SMOKE_ENV}")
add_test(NAME app_smoke_diagram_export COMMAND qivot-studio --open-sample company --export-diagram ${CMAKE_CURRENT_BINARY_DIR}/company.pdf --smoke --shot-delay 2000)
set_tests_properties(app_smoke_diagram_export PROPERTIES ENVIRONMENT "${STUDIO_SMOKE_ENV}")
add_test(NAME app_smoke_welcome COMMAND qivot-studio --smoke)
set_tests_properties(app_smoke_welcome PROPERTIES ENVIRONMENT "${STUDIO_SMOKE_ENV}")

# The server scripts in tools/sample-servers/sql must match the samples.
if(NOT WIN32)
    add_test(NAME sample_server_scripts
             COMMAND bash ${PROJECT_SOURCE_DIR}/tools/sample-servers/generate.sh --check $<TARGET_FILE:qivot-studio>)
    set_tests_properties(sample_server_scripts PROPERTIES ENVIRONMENT "${STUDIO_SMOKE_ENV}")
endif()

# The connect dialog must load cleanly too.
add_test(NAME app_smoke_connect COMMAND qivot-studio --connect-dialog --smoke)
set_tests_properties(app_smoke_connect PROPERTIES ENVIRONMENT "${STUDIO_SMOKE_ENV}")

# The SQL console, running a real query against the sample.
add_test(NAME app_smoke_query COMMAND qivot-studio --sample --smoke
         --query "SELECT a.country, COUNT(*) AS books FROM book b JOIN author a ON a.id = b.author_id GROUP BY 1")
set_tests_properties(app_smoke_query PROPERTIES ENVIRONMENT "${STUDIO_SMOKE_ENV}")

# The C++ tab, on a table with foreign keys.
add_test(NAME app_smoke_cpp COMMAND qivot-studio --sample --table book --view cpp --smoke)
set_tests_properties(app_smoke_cpp PROPERTIES ENVIRONMENT "${STUDIO_SMOKE_ENV}")

# The Export screen: generates and previews the project (writes nothing).
add_test(NAME app_smoke_export COMMAND qivot-studio --sample --view export --smoke)
set_tests_properties(app_smoke_export PROPERTIES ENVIRONMENT "${STUDIO_SMOKE_ENV}")

# The IDE, open on a real CMake project (Studio's own sources; nothing is written).
add_test(NAME app_smoke_ide COMMAND qivot-studio --sample --project ${PROJECT_SOURCE_DIR} --smoke)
set_tests_properties(app_smoke_ide PROPERTIES ENVIRONMENT "${STUDIO_SMOKE_ENV}")

# The class designer, with edits made (canvas, inspector, C++ and SQL).
add_test(NAME app_smoke_design COMMAND qivot-studio --sample --view design --design-demo --table book_tag --smoke)
set_tests_properties(app_smoke_design PROPERTIES ENVIRONMENT "${STUDIO_SMOKE_ENV}")
add_test(NAME app_smoke_design_sql COMMAND qivot-studio --sample --view design --design-demo --design-tab sql --smoke)
set_tests_properties(app_smoke_design_sql PROPERTIES ENVIRONMENT "${STUDIO_SMOKE_ENV}")

# The query builder, starting on the first table.
add_test(NAME app_smoke_builder COMMAND qivot-studio --sample --builder-demo --smoke)
set_tests_properties(app_smoke_builder PROPERTIES ENVIRONMENT "${STUDIO_SMOKE_ENV}")

# Column profiles, on the biggest table.
add_test(NAME app_smoke_profile COMMAND qivot-studio --sample --table order_item --view profile --smoke --shot-delay 3000)
set_tests_properties(app_smoke_profile PROPERTIES ENVIRONMENT "${STUDIO_SMOKE_ENV}")

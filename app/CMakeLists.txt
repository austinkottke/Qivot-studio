# The Qivot Studio app: main.cpp plus the screens in qml/.
set(STUDIO_QML_FILES
    qml/Main.qml
    qml/WelcomeView.qml
    qml/SchemaSidebar.qml
    qml/TableDetail.qml
    qml/DataView.qml
    qml/DiagramView.qml
    qml/ConnectDialog.qml
    qml/ResultGrid.qml
    qml/QueryView.qml
    qml/OpenDialog.qml
    qml/ModelView.qml
    qml/ProjectView.qml
    qml/SamplesView.qml
    qml/AllowChangesDialog.qml
    qml/ExportButton.qml
    qml/ImportDialog.qml
    qml/CompareView.qml
    qml/FolderPicker.qml
    qml/IdeView.qml
    qml/CodeEditor.qml
    qml/FileIcon.qml
    qml/StepPill.qml
    qml/DesignerView.qml
    qml/DesignFileDialog.qml
    qml/DataFileDialog.qml
    qml/QueryBuilderView.qml
    qml/ProfileView.qml
)

# The icon (tools/make-icon.py): .icns in the macOS bundle, .ico in the Windows
# .exe, and a PNG in the resources for the window icon everywhere.
set(STUDIO_ICON_SOURCES icons/icons.qrc)
if(APPLE)
    set_source_files_properties(icons/QivotStudio.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    list(APPEND STUDIO_ICON_SOURCES icons/QivotStudio.icns)
elseif(WIN32)
    list(APPEND STUDIO_ICON_SOURCES icons/QivotStudio.rc)
endif()

if(QT_VERSION_MAJOR EQUAL 6)
    set(CMAKE_AUTORCC ON)
    qt_add_executable(qivot-studio main.cpp ${STUDIO_ICON_SOURCES})
    qt_add_qml_module(qivot-studio
        URI QivotStudio
        VERSION 1.0
        QML_FILES ${STUDIO_QML_FILES})
    target_link_libraries(qivot-studio PRIVATE
        Qt6::Gui Qt6::Quick Qt6::QuickControls2
        studiocore studiocoreplugin
        qivotui qivotuiplugin)
else()
    # Qt 5: one resource file with both QML modules, laid out as
    # qrc:/qml/QivotStudio/*.qml and qrc:/qml/QivotUI/{qmldir,*.qml}.
    include(${PROJECT_SOURCE_DIR}/compat/qt5/qmlresources.cmake)
    studio_qt5_resources(${CMAKE_CURRENT_BINARY_DIR}/studio_qml.qrc)
    add_executable(qivot-studio main.cpp ${CMAKE_CURRENT_BINARY_DIR}/studio_qml.qrc ${STUDIO_ICON_SOURCES})
    target_link_libraries(qivot-studio PRIVATE Qt5::Gui Qt5::Quick Qt5::QuickControls2 studiocore)
endif()

set_target_properties(qivot-studio PROPERTIES
    OUTPUT_NAME "Qivot Studio"
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER io.qivot.studio
    MACOSX_BUNDLE_BUNDLE_NAME "Qivot Studio"
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_ICON_FILE QivotStudio.icns
    MACOSX_BUNDLE_COPYRIGHT "Copyright © 2026 Austin Kottke. MIT License."
    WIN32_EXECUTABLE TRUE)
target_compile_definitions(qivot-studio PRIVATE PROJECT_VERSION_STRING="${PROJECT_VERSION}")

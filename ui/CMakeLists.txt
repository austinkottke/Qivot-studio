# QivotUI — qivot-ui v0: theme tokens and components shared by Studio's screens.
# Kept free of Studio specifics so it can graduate to its own repo.
set(QIVOTUI_QML_FILES
    Theme.qml
    ActionButton.qml
    Badge.qml
    Card.qml
    FilterField.qml
    NavIcon.qml
    NavItem.qml
    SegmentedControl.qml
    TextBox.qml
    Field.qml
    Choice.qml
    Toggle.qml
    ContextMenu.qml
    ContextMenuItem.qml
    ContextMenuSeparator.qml
)
set(QIVOTUI_QML_FILES ${QIVOTUI_QML_FILES} PARENT_SCOPE)   # the Qt 5 build packs them in app/

if(QT_VERSION_MAJOR EQUAL 6)
    set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
    qt_add_library(qivotui STATIC)
    qt_add_qml_module(qivotui
        URI QivotUI
        VERSION 1.0
        OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/QivotUI
        QML_FILES ${QIVOTUI_QML_FILES})
    target_link_libraries(qivotui PRIVATE Qt6::Quick)
endif()

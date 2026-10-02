# QivotStudio.Core — everything that talks to a database.
# A static QML module: the app imports it as `QivotStudio.Core`, and the tests
# link the same code directly.
set(STUDIOCORE_SOURCES
    databasesession.h databasesession.cpp
    sampledatabase.h  sampledatabase.cpp
    sampleschema.h    sampleschema.cpp
    sqlscript.h       sqlscript.cpp
    datatransfer.h    datatransfer.cpp
    schemacompare.h   schemacompare.cpp
    diagramexport.h   diagramexport.cpp
    querylibrary.h    querylibrary.cpp
    sqlcompleter.h    sqlcompleter.cpp
    queryplan.h       queryplan.cpp
    rowsmodel.h       rowsmodel.cpp
    erlayout.h        erlayout.cpp
    diagramgeometry.h
    diagramdata.h     diagramdata.cpp
    cellformat.h
    querymodel.h      querymodel.cpp
    syntaxhighlighter.h syntaxhighlighter.cpp
    prefs.h
    codegen.h         codegen.cpp
    projectexport.h   projectexport.cpp
    projectbuild.h    projectbuild.cpp
    workspace.h       workspace.cpp
    schemadesign.h    schemadesign.cpp
    querybuilder.h    querybuilder.cpp
    tableprofile.h    tableprofile.cpp
)
# Qivot's single header, carried as a resource so exported projects get a copy.
qt_add_resources(STUDIOCORE_SOURCES qivot.qrc)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_add_library(studiocore STATIC)
    qt_add_qml_module(studiocore
        URI QivotStudio.Core
        VERSION 1.0
        OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/QivotStudio/Core
        SOURCES ${STUDIOCORE_SOURCES})
else()
    # Qt 5: the types are registered by hand in app/main.cpp.
    add_library(studiocore STATIC ${STUDIOCORE_SOURCES})
endif()
target_include_directories(studiocore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(studiocore PUBLIC Qt::Core Qt::Gui Qt::Qml Qt::Quick Qt::Sql qivot)

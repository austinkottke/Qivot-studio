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
    gridtools.h       gridtools.cpp
    connectionhistory.h connectionhistory.cpp
    sshtunnel.h       sshtunnel.cpp
    redisclient.h     redisclient.cpp
    redissession.h    redissession.cpp
    listtablemodel.h  listtablemodel.cpp
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
target_link_libraries(studiocore PUBLIC Qt::Core Qt::Gui Qt::Network Qt::Qml Qt::Quick Qt::Sql qivot)

# DuckDB files, through Qivot's QDUCKDB driver (see cmake/duckdb.cmake).
if(STUDIO_HAS_DUCKDB)
    target_sources(studiocore PRIVATE
        ${PROJECT_SOURCE_DIR}/third_party/qivot/drivers/duckdb/duckdbdriver.h
        ${PROJECT_SOURCE_DIR}/third_party/qivot/drivers/duckdb/duckdbdriver.cpp)
    target_include_directories(studiocore PRIVATE ${PROJECT_SOURCE_DIR}/third_party/qivot/drivers/duckdb)
    target_link_libraries(studiocore PUBLIC duckdb::duckdb)
    target_compile_definitions(studiocore PUBLIC STUDIO_HAS_DUCKDB)
endif()

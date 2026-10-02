cmake_minimum_required(VERSION 3.21)

project(QivotStudio
    VERSION 0.1.0
    DESCRIPTION "Database designer and analyzer built on Qivot"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt 6.5+ is the main target; Qt 5.15 builds too, with QML modules assembled
# by hand (see compat/qt5/) since Qt 5's CMake has no qt_add_qml_module.
set(STUDIO_QT_COMPONENTS Core Gui Network Qml Quick QuickControls2 Sql Test)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
if(QT_VERSION_MAJOR EQUAL 6)
    find_package(Qt6 6.5 REQUIRED COMPONENTS ${STUDIO_QT_COMPONENTS})
    qt_standard_project_setup(REQUIRES 6.5)
    if(QT_KNOWN_POLICY_QTP0004)
        qt_policy(SET QTP0004 NEW)   # qmldir files for QML in subfolders (app/qml/)
    endif()
else()
    find_package(Qt5 5.15 REQUIRED COMPONENTS ${STUDIO_QT_COMPONENTS})
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
endif()

# --- Qivot, as its single header (third_party/qivot; tools/update-qivot.sh) ---
add_library(qivot STATIC third_party/qivot/qivot.cpp third_party/qivot/qivot.hpp)
target_include_directories(qivot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/qivot)
target_link_libraries(qivot PUBLIC Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Sql)
# QiModel takes offsetof() of its models on purpose; the warning is noise.
if(NOT MSVC)
    target_compile_options(qivot PUBLIC -Wno-invalid-offsetof)
endif()

add_subdirectory(core)    # QivotStudio.Core: database logic
add_subdirectory(ui)      # QivotUI: theme + components (qivot-ui v0)
add_subdirectory(app)     # the Qivot Studio app

enable_testing()
add_subdirectory(tests)

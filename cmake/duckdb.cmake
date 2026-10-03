# DuckDB's C library, for opening .duckdb files (Qivot's QDUCKDB driver,
# third_party/qivot/drivers/duckdb). Defines the imported target `duckdb::duckdb`
# and STUDIO_HAS_DUCKDB.
#
#   -DSTUDIO_DUCKDB=OFF       build without DuckDB
#   -DDUCKDB_ROOT=<folder>    use this unzipped libduckdb-* release (duckdb.h and the
#                             library) instead of downloading one
#
# Otherwise the release for this platform is downloaded once into the build folder.

option(STUDIO_DUCKDB "Open DuckDB files (downloads DuckDB's C library unless DUCKDB_ROOT is set)" ON)
set(STUDIO_DUCKDB_VERSION "1.5.6" CACHE STRING "DuckDB release to download")
set(STUDIO_HAS_DUCKDB OFF)

if(STUDIO_DUCKDB)
    if(NOT DUCKDB_ROOT)
        if(APPLE)
            set(_asset osx-universal)
        elseif(WIN32)
            if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
                set(_asset windows-arm64)
            else()
                set(_asset windows-amd64)
            endif()
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
            set(_asset linux-arm64)
        else()
            set(_asset linux-amd64)
        endif()
        set(DUCKDB_ROOT "${CMAKE_BINARY_DIR}/_deps/libduckdb-${STUDIO_DUCKDB_VERSION}-${_asset}")
        if(NOT EXISTS "${DUCKDB_ROOT}/duckdb.h")
            set(_url "https://github.com/duckdb/duckdb/releases/download/v${STUDIO_DUCKDB_VERSION}/libduckdb-${_asset}.zip")
            message(STATUS "Downloading DuckDB ${STUDIO_DUCKDB_VERSION}: ${_url}")
            file(DOWNLOAD "${_url}" "${DUCKDB_ROOT}.zip" STATUS _status SHOW_PROGRESS)
            list(GET _status 0 _code)
            if(NOT _code EQUAL 0)
                message(FATAL_ERROR "Couldn't download DuckDB (${_status}). Set DUCKDB_ROOT to an unzipped "
                                    "libduckdb release, or -DSTUDIO_DUCKDB=OFF to build without it.")
            endif()
            file(ARCHIVE_EXTRACT INPUT "${DUCKDB_ROOT}.zip" DESTINATION "${DUCKDB_ROOT}")
            file(REMOVE "${DUCKDB_ROOT}.zip")
        endif()
    endif()

    add_library(duckdb::duckdb SHARED IMPORTED GLOBAL)
    set_target_properties(duckdb::duckdb PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${DUCKDB_ROOT}")
    if(WIN32)
        set_target_properties(duckdb::duckdb PROPERTIES
            IMPORTED_LOCATION "${DUCKDB_ROOT}/duckdb.dll"
            IMPORTED_IMPLIB "${DUCKDB_ROOT}/duckdb.lib")
    elseif(APPLE)
        set_target_properties(duckdb::duckdb PROPERTIES IMPORTED_LOCATION "${DUCKDB_ROOT}/libduckdb.dylib")
    else()
        set_target_properties(duckdb::duckdb PROPERTIES IMPORTED_LOCATION "${DUCKDB_ROOT}/libduckdb.so")
    endif()
    set(STUDIO_HAS_DUCKDB ON)
    set(STUDIO_DUCKDB_DLL "${DUCKDB_ROOT}/duckdb.dll")
endif()

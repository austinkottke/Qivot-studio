# qivot-cli: Studio's engine (studiocore) from the command line.
add_library(qivotcli STATIC qivotcli.cpp qivotcli.h)
target_include_directories(qivotcli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qivotcli PUBLIC studiocore)

add_executable(qivot-cli main.cpp)
target_link_libraries(qivot-cli PRIVATE qivotcli)
target_compile_definitions(qivot-cli PRIVATE PROJECT_VERSION_STRING="${PROJECT_VERSION}")

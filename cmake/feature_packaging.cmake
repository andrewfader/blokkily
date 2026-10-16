# Feature fragment: packaging (features/packaging.feature): AppStream
# metadata installed with the app, an AppImage script and a Flatpak manifest
# under packaging/, and a check that they agree.

# AppStream, AppImage and Flatpak are Linux packaging, and the check reuses
# desktop-file-validate from feature_open_from_desktop.cmake, so it runs
# where that one does.
if(BLOKKILY_BUILD_TESTS AND BLOKKILY_BUILD_GUI AND UNIX AND NOT APPLE)
    find_program(BLOKKILY_XMLLINT xmllint REQUIRED)
    find_program(BLOKKILY_BASH bash REQUIRED)
    add_test(NAME packaging_metadata COMMAND ${CMAKE_COMMAND}
        -DSOURCE=${CMAKE_SOURCE_DIR}
        -DMETAINFO=${CMAKE_BINARY_DIR}/dev.blokkily.Blokkily.metainfo.xml
        -DVERSION=${PROJECT_VERSION}
        -DXMLLINT=${BLOKKILY_XMLLINT}
        -DVALIDATE=${BLOKKILY_DESKTOP_VALIDATE}
        -DBASH=${BLOKKILY_BASH}
        -P ${CMAKE_SOURCE_DIR}/tests/packaging_check.cmake)
    set_tests_properties(packaging_metadata PROPERTIES LABELS "unit;packaging;desktop" TIMEOUT 30)
endif()

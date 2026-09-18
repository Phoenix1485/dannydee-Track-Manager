cmake_minimum_required(VERSION 3.21)
project(DannyDeeTrackManager VERSION 0.10.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Sql Network Multimedia)

set(DANNYDEE_UPDATE_MANIFEST_URL "" CACHE STRING
    "HTTPS URL of the JSON update manifest used by packaged builds")
configure_file(src/AppConfig.h.in generated/AppConfig.h @ONLY)

qt_add_executable(DannyDeeTrackManager WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/MainWindow.cpp src/MainWindow.h
    src/Database.cpp src/Database.h
    src/LinkResolver.cpp src/LinkResolver.h
    src/AudioConverter.cpp src/AudioConverter.h
    src/DownloadManager.cpp src/DownloadManager.h
    src/AudioProbe.cpp src/AudioProbe.h
    src/ToolLocator.cpp src/ToolLocator.h
    src/UpdateManager.cpp src/UpdateManager.h
    src/PlaylistImporter.cpp src/PlaylistImporter.h
    src/BeatAnalyzer.cpp src/BeatAnalyzer.h
    src/BeatGridWidget.cpp src/BeatGridWidget.h
    src/BeatSyncMath.cpp src/BeatSyncMath.h
    resources/resources.qrc
)

target_include_directories(DannyDeeTrackManager PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_link_libraries(DannyDeeTrackManager PRIVATE
    Qt6::Widgets Qt6::Sql Qt6::Network Qt6::Multimedia
)

set_target_properties(DannyDeeTrackManager PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "DannyDee Track Manager"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.dannydee.trackmanager"
    MACOSX_BUNDLE_ICON_FILE "app.icns"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

install(TARGETS DannyDeeTrackManager
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

include(CTest)
if(BUILD_TESTING)
    qt_add_executable(DannyDeeDownloadFallbackTest
        tests/DownloadManagerFallbackTest.cpp
        src/DownloadManager.cpp src/DownloadManager.h
        src/ToolLocator.cpp src/ToolLocator.h
    )
    set_target_properties(DannyDeeDownloadFallbackTest PROPERTIES WIN32_EXECUTABLE FALSE)
    target_include_directories(DannyDeeDownloadFallbackTest PRIVATE
        src "${CMAKE_CURRENT_BINARY_DIR}/generated"
    )
    target_link_libraries(DannyDeeDownloadFallbackTest PRIVATE Qt6::Core Qt6::Network)
    add_test(NAME spotify_strict_source_mode COMMAND DannyDeeDownloadFallbackTest)

    qt_add_executable(DannyDeeUpdateVersionTest
        tests/UpdateManagerVersionTest.cpp
        src/UpdateManager.cpp src/UpdateManager.h
    )
    set_target_properties(DannyDeeUpdateVersionTest PROPERTIES WIN32_EXECUTABLE FALSE)
    target_include_directories(DannyDeeUpdateVersionTest PRIVATE
        src "${CMAKE_CURRENT_BINARY_DIR}/generated"
    )
    target_link_libraries(DannyDeeUpdateVersionTest PRIVATE Qt6::Core Qt6::Network)
    add_test(NAME update_version_comparison COMMAND DannyDeeUpdateVersionTest)

    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME multi_platform_update_site
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/UpdateSiteGeneratorTest.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/create_update_site.py"
        )
    endif()

    qt_add_executable(DannyDeePlaylistTest
        tests/PlaylistFeatureTest.cpp
        src/Database.cpp src/Database.h
        src/PlaylistImporter.cpp src/PlaylistImporter.h
        src/ToolLocator.cpp src/ToolLocator.h
    )
    set_target_properties(DannyDeePlaylistTest PROPERTIES WIN32_EXECUTABLE FALSE)
    target_include_directories(DannyDeePlaylistTest PRIVATE src)
    target_link_libraries(DannyDeePlaylistTest PRIVATE Qt6::Core Qt6::Sql)
    add_test(NAME playlist_import_and_membership COMMAND DannyDeePlaylistTest)

    qt_add_executable(DannyDeeBeatAnalyzerTest
        tests/BeatAnalyzerTest.cpp
        src/BeatAnalyzer.cpp src/BeatAnalyzer.h
        src/ToolLocator.cpp src/ToolLocator.h
    )
    set_target_properties(DannyDeeBeatAnalyzerTest PROPERTIES WIN32_EXECUTABLE FALSE)
    target_include_directories(DannyDeeBeatAnalyzerTest PRIVATE src)
    target_link_libraries(DannyDeeBeatAnalyzerTest PRIVATE Qt6::Core)
    add_test(NAME bpm_and_beatgrid_analysis COMMAND DannyDeeBeatAnalyzerTest)

    qt_add_executable(DannyDeeBeatSyncMathTest
        tests/BeatSyncMathTest.cpp
        src/BeatSyncMath.cpp src/BeatSyncMath.h
    )
    set_target_properties(DannyDeeBeatSyncMathTest PROPERTIES WIN32_EXECUTABLE FALSE)
    target_include_directories(DannyDeeBeatSyncMathTest PRIVATE src)
    target_link_libraries(DannyDeeBeatSyncMathTest PRIVATE Qt6::Core)
    add_test(NAME beat_sync_math COMMAND DannyDeeBeatSyncMathTest)

    qt_add_executable(DannyDeeMainWindowUiSmokeTest
        tests/MainWindowUiSmokeTest.cpp
        src/MainWindow.cpp src/MainWindow.h
        src/Database.cpp src/Database.h
        src/LinkResolver.cpp src/LinkResolver.h
        src/AudioConverter.cpp src/AudioConverter.h
        src/DownloadManager.cpp src/DownloadManager.h
        src/AudioProbe.cpp src/AudioProbe.h
        src/ToolLocator.cpp src/ToolLocator.h
        src/UpdateManager.cpp src/UpdateManager.h
        src/PlaylistImporter.cpp src/PlaylistImporter.h
        src/BeatAnalyzer.cpp src/BeatAnalyzer.h
        src/BeatGridWidget.cpp src/BeatGridWidget.h
        src/BeatSyncMath.cpp src/BeatSyncMath.h
    )
    set_target_properties(DannyDeeMainWindowUiSmokeTest PROPERTIES WIN32_EXECUTABLE FALSE)
    target_include_directories(DannyDeeMainWindowUiSmokeTest PRIVATE
        src "${CMAKE_CURRENT_BINARY_DIR}/generated"
    )
    target_link_libraries(DannyDeeMainWindowUiSmokeTest PRIVATE
        Qt6::Widgets Qt6::Sql Qt6::Network Qt6::Multimedia
    )
    add_test(NAME main_window_ui_smoke COMMAND DannyDeeMainWindowUiSmokeTest)
    set_tests_properties(main_window_ui_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

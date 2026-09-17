cmake_minimum_required(VERSION 3.21)
project(DannyDeeTrackManager VERSION 0.6.0 LANGUAGES CXX)

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
    add_executable(DannyDeeFakeSpotDl tests/FakeSpotDl.cpp)
    set_target_properties(DannyDeeFakeSpotDl PROPERTIES
        OUTPUT_NAME "spotdl"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-tools"
    )

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
    add_test(NAME spotify_external_ytdlp_fallback COMMAND DannyDeeDownloadFallbackTest)
    set_tests_properties(spotify_external_ytdlp_fallback PROPERTIES
        ENVIRONMENT "DANNYDEE_TOOLS_DIR=${CMAKE_BINARY_DIR}/test-tools"
        TIMEOUT 120
    )

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
endif()

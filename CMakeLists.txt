cmake_minimum_required(VERSION 3.24)

project(reolink-client
    VERSION 0.1.0
    DESCRIPTION "Native Linux client for Reolink cameras and NVRs"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core Gui Qml Quick QuickControls2 Sql Multimedia Concurrent Network LinguistTools Test ShaderTools)
qt_standard_project_setup(REQUIRES 6.5)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat libavcodec libavutil libswscale)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(SECRET REQUIRED IMPORTED_TARGET libsecret-1)

# ---------------------------------------------------------------------------
# Core library: protocol, storage, media — everything testable without a GUI
# ---------------------------------------------------------------------------
add_library(reolink_core STATIC
    src/core/Log.h src/core/Log.cpp
    src/core/Paths.h src/core/Paths.cpp
    src/core/Database.h src/core/Database.cpp
    src/core/CredentialStore.h src/core/CredentialStore.cpp
    src/protocol/Json.h
    src/protocol/ReolinkApi.h src/protocol/ReolinkApi.cpp
    src/protocol/ReolinkHttpClient.h src/protocol/ReolinkHttpClient.cpp
    src/protocol/BcCrypto.h src/protocol/BcCrypto.cpp
    src/device/DeviceManager.h src/device/DeviceManager.cpp
    src/device/DeviceDiscovery.h src/device/DeviceDiscovery.cpp
    src/device/EventManager.h src/device/EventManager.cpp
    src/media/StreamPlayer.h src/media/StreamPlayer.cpp
)
target_include_directories(reolink_core PUBLIC src third_party)
target_link_libraries(reolink_core PUBLIC
    Qt6::Core Qt6::Gui Qt6::Sql Qt6::Multimedia Qt6::Concurrent Qt6::Network
    PkgConfig::FFMPEG PkgConfig::CURL PkgConfig::SECRET)

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
qt_add_executable(reolink-client src/main.cpp)

set(APP_QML_FILES
    src/ui/qml/Main.qml
    src/ui/qml/Theme.qml
    src/ui/qml/components/SideBar.qml
    src/ui/qml/components/NavBar.qml
    src/ui/qml/components/LivePane.qml
    src/ui/qml/components/PtzPad.qml
    src/ui/qml/components/Timeline.qml
    src/ui/qml/components/MonthCalendar.qml
    src/ui/qml/components/SettingRow.qml
    src/ui/qml/components/CameraComboBox.qml
    src/ui/qml/components/DoorbellOverlay.qml
    src/ui/qml/components/PoppedWindow.qml
    src/ui/qml/components/FisheyeView.qml
    src/ui/qml/pages/LiveViewPage.qml
    src/ui/qml/pages/PlaybackPage.qml
    src/ui/qml/pages/EventsPage.qml
    src/ui/qml/pages/DeviceSettingsPage.qml
    src/ui/qml/dialogs/AddDeviceDialog.qml
)
# Strip the src/ui/qml/ prefix so module paths are clean (Main.qml, pages/..., etc.)
foreach(qml_file IN LISTS APP_QML_FILES)
    string(REPLACE "src/ui/qml/" "" qml_alias "${qml_file}")
    set_source_files_properties(${qml_file} PROPERTIES QT_RESOURCE_ALIAS "${qml_alias}")
endforeach()
set_source_files_properties(src/ui/qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(reolink-client
    URI ReolinkApp
    VERSION 1.0
    QML_FILES ${APP_QML_FILES}
)

# Fisheye dewarp shader → compiled .qsb, exposed under the module's qml resource
# path so FisheyeView can reference it as qrc:/qt/qml/ReolinkApp/shaders/...
qt_add_shaders(reolink-client "reolink_shaders"
    PREFIX "/qt/qml/ReolinkApp"
    FILES src/ui/shaders/dewarp.frag
    OUTPUTS shaders/dewarp.frag.qsb
)

target_link_libraries(reolink-client PRIVATE reolink_core Qt6::QuickControls2)

qt_add_translations(reolink-client
    TS_FILES i18n/reolink-client_en.ts i18n/reolink-client_es.ts
    LUPDATE_OPTIONS -no-obsolete)

install(TARGETS reolink-client RUNTIME DESTINATION bin)
install(FILES packaging/io.github.todesengelx.ReolinkLinux.desktop
        DESTINATION share/applications)
install(FILES packaging/io.github.todesengelx.ReolinkLinux.metainfo.xml
        DESTINATION share/metainfo)
install(FILES packaging/io.github.todesengelx.ReolinkLinux.svg
        DESTINATION share/icons/hicolor/scalable/apps)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
foreach(t IN ITEMS test_protocol test_database)
    qt_add_executable(${t} tests/${t}.cpp)
    target_link_libraries(${t} PRIVATE reolink_core Qt6::Test)
    add_test(NAME ${t} COMMAND ${t})
    set_tests_properties(${t} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

cmake_minimum_required(VERSION 3.19)
project(DesktopAiHelper LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)
find_package(Qt6 REQUIRED COMPONENTS Network)
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Qt6 REQUIRED COMPONENTS TextToSpeech)

set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOMOC ON)
qt_standard_project_setup()

qt_add_executable(DesktopAiHelper
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui

    aiclient.h aiclient.cpp
    geminiclient.h geminiclient.cpp
    resource.qrc
    README.md
    chatgptclient.h chatgptclient.cpp








)

target_link_libraries(DesktopAiHelper
    PRIVATE
    Qt::Core
    Qt::Widgets
    Qt::Network
)
target_link_libraries(DesktopAiHelper PRIVATE Qt6::Core)
target_link_libraries(DesktopAiHelper PRIVATE Qt6::Core)
target_link_libraries(DesktopAiHelper PRIVATE Qt6::TextToSpeech)
include(GNUInstallDirs)

install(TARGETS DesktopAiHelper
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET DesktopAiHelper
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

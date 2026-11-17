cmake_minimum_required(VERSION 3.21)
project(Tidy VERSION 1.1.6 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# App icon (shown in Dock, Finder and the app switcher)
set(APP_ICON ${CMAKE_CURRENT_SOURCE_DIR}/icon/Tidy.icns)
set_source_files_properties(${APP_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

add_executable(Tidy MACOSX_BUNDLE main.cpp ${APP_ICON})
target_link_libraries(Tidy PRIVATE Qt6::Widgets)

set_target_properties(Tidy PROPERTIES
    MACOSX_BUNDLE_ICON_FILE Tidy.icns
    MACOSX_BUNDLE_BUNDLE_NAME Tidy
    MACOSX_BUNDLE_GUI_IDENTIFIER com.yourname.tidy
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
)

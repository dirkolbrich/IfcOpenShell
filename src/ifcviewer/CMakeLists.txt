cmake_minimum_required(VERSION 3.21)

project(ifcviewer VERSION 0.0.1
                DESCRIPTION "A basic IFC viewer app."
                LANGUAGES CXX)

# set build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

# set C++ standards
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED On)
set(CMAKE_CXX_EXTENSIONS Off)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# Qt base setup
find_package(Qt6 REQUIRED COMPONENTS Gui Qml QuickControls2)
qt_standard_project_setup(REQUIRES 6.5)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(${PROJECT_NAME}
    main.cpp
)

# add_subdirectory(views)
add_subdirectory(qml/IfcViewer/Ui)

target_link_libraries(${PROJECT_NAME} PRIVATE Qt::Gui Qt::Qml Qt::QuickControls2 ifcviewerUi)

set_target_properties(${PROJECT_NAME} PROPERTIES
    MACOSX_BUNDLE ON
)

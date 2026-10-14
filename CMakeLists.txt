cmake_minimum_required(VERSION 3.24)
project(AOBakerProStandalone VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(AO_BAKER_STATIC_RUNTIME "Use static MSVC runtime" ON)

if(MSVC AND AO_BAKER_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets Gui Core)
find_package(assimp CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

qt_add_executable(AO_Baker_Pro_Standalone WIN32
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/ModelData.h
    src/ModelData.cpp
    src/ModelIO.h
    src/ModelIO.cpp
    src/AOBaker.h
    src/AOBaker.cpp
    src/TextureRasterizer.h
    src/TextureRasterizer.cpp
    src/ViewportWidget.h
    src/ViewportWidget.cpp
)

target_link_libraries(AO_Baker_Pro_Standalone PRIVATE
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::Gui
    Qt6::Core
    assimp::assimp
    glm::glm
    ZLIB::ZLIB
)

target_compile_definitions(AO_Baker_Pro_Standalone PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
    NOMINMAX
)

if(MSVC)
    target_compile_options(AO_Baker_Pro_Standalone PRIVATE /O2 /MP /EHsc)
endif()

install(TARGETS AO_Baker_Pro_Standalone RUNTIME DESTINATION .)

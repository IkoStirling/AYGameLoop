cmake_minimum_required(VERSION 3.20)

project(AYGameLoop)

set(SOURCES
    src/AYGameLoopImpl.cpp
    src/AYSubSystemRegistry.cpp
    src/AYSubSystemModule.cpp
    src/AYFrameManager.cpp
    src/AYFrameStats.cpp
    src/AYFrameInterpolator.cpp
    src/AYRenderThreadSync.cpp
    src/AYJobScheduler.cpp
)

add_library(${PROJECT_NAME} STATIC ${SOURCES})

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/interface
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    AY::Module
    AYCore
    AYMath
    AYPlatform
    AYTime
    AYLog
    AYTask
    AYEventSystem
)


enable_testing()
add_subdirectory(unittest)

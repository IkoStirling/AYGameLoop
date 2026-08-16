cmake_minimum_required(VERSION  3.20)

project(AYGameLoop_Test)

set(SOURCES
    TestMain.cpp
    GameLoopTest.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../interface
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../AYFoundation/AYTest
    ${CMAKE_CURRENT_SOURCE_DIR}/../../AYFoundation/AYPlatform/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../AYFoundation/AYTime/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../AYFoundation/AYTask/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    AYGameLoop
    AYTest
    AYTask
)

target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>: /W4 /wd4200>
    $<$<CXX_COMPILER_ID:GNU,Clang>: -Wall -Wextra -Wpedantic>
)

ay_add_test(${PROJECT_NAME})

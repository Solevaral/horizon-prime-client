cmake_minimum_required(VERSION 3.20)
project(horizon-prime-client CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(asio  CONFIG REQUIRED)
find_package(glad  CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(Stb   REQUIRED)

set(HP_CLIENT_SOURCES
    src/main.cpp
    src/state.cpp
    src/net.cpp
    src/terminal.cpp
    src/sound.cpp
    src/settings.cpp
    src/stat_overlay.cpp
    src/scrollback_overlay.cpp
    src/report_overlay.cpp
    src/map_overlay.cpp
    src/obfuscate.cpp
)

if(WIN32)
    add_executable(hp-client WIN32 ${HP_CLIENT_SOURCES})
else()
    add_executable(hp-client ${HP_CLIENT_SOURCES})
endif()

target_include_directories(hp-client PRIVATE
    ${Stb_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../horizon-prime-proto
)

target_link_libraries(hp-client PRIVATE
    asio::asio
    glad::glad
    glfw
)

if(WIN32)
    target_link_libraries(hp-client PRIVATE ws2_32 wsock32 opengl32)
endif()

target_compile_definitions(hp-client PRIVATE
    ASIO_STANDALONE
    _WIN32_WINNT=0x0601
)

# Copy assets next to executable
add_custom_command(TARGET hp-client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:hp-client>/assets
    COMMENT "Copying assets"
)

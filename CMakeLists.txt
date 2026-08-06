cmake_minimum_required(VERSION 3.10)
project(Hello_chess)

set(CMAKE_CXX_STANDARD 17)

include(cmake-local.cmake OPTIONAL)

# ========== 只找头文件路径，不找库 ==========
find_path(EASYX_INCLUDE
    NAMES easyx.h
    PATHS
        "${EASYX_LOCAL_PATH}/include"
        "C:/Program Files (x86)/EasyX/include"
        "C:/EasyX/include"
    REQUIRED
)

message(STATUS "已找到EasyX头文件: ${EASYX_INCLUDE}")

# ========== 源文件 ==========
set(SOURCE_FILES
    main.cpp
    game_init.cpp
    game_render.cpp
    game_update.cpp
    game_judge.cpp
    game_ai.cpp
    game_mode.cpp
)

add_executable(${PROJECT_NAME} ${SOURCE_FILES})

target_include_directories(${PROJECT_NAME} PRIVATE ${EASYX_INCLUDE})

# ========== 告诉链接器去哪个目录找库 ==========
target_link_directories(${PROJECT_NAME} PRIVATE
    "${EASYX_LOCAL_PATH}/lib/x64"
    "C:/Program Files (x86)/EasyX/lib/x64"
    "C:/EasyX/lib/x64"
)

# ========== 只链系统库，不手动链 EasyX ==========
target_link_libraries(${PROJECT_NAME}
    gdi32 ole32 oleaut32 winmm msimg32
)

# ========== UNICODE ==========
target_compile_definitions(${PROJECT_NAME} PRIVATE UNICODE _UNICODE)
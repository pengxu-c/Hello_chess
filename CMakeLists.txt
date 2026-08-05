cmake_minimum_required(VERSION 3.10)
project(Hello_chess)

set(CMAKE_CXX_STANDARD 11)

# =============================================
# 引入本地用户配置
# =============================================
include(cmake-local.cmake OPTIONAL)

# =============================================
# 查找 第三方库（优先使用本地配置的路径）
# =============================================
find_library(EASYX_LIB
    NAMES EasyXa
    PATHS
        "${EASYX_LOCAL_PATH}/lib/x64"        # 从这里读你的路径
        "C:/Program Files (x86)/EasyX/lib/x64"
        "C:/EasyX/lib/x64"
    REQUIRED
)

find_path(EASYX_INCLUDE
    NAMES easyx.h
    PATHS
        "${EASYX_LOCAL_PATH}/include"        # 从这里读你的路径
        "C:/Program Files (x86)/EasyX/include"
        "C:/EasyX/include"
    REQUIRED
)

message(STATUS "已找到EasyX库: ${EASYX_LIB}")
message(STATUS "已找到EasyX头文件: ${EASYX_INCLUDE}")

# =============================================
# 源文件列表
# =============================================
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

target_link_libraries(${PROJECT_NAME}
    ${EASYX_LIB}
    gdi32 ole32 oleaut32 winmm msimg32
)
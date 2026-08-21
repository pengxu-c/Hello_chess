cmake_minimum_required(VERSION 3.15)
project(Hello_chess)
set(X_VCPKG_APPLOCAL_DEPS_INSTALL ON)
set(CMAKE_CXX_STANDARD 17)
if(MSVC)
    add_compile_options(/utf-8)
else()
    add_compile_options(-finput-charset=UTF-8 -fexec-charset=GBK)
endif()

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
    core.cpp
    ui.cpp
    player.cpp
    ai_config.cpp
    ai_player.cpp
    controller.cpp
    storage.cpp
)

add_executable(${PROJECT_NAME} ${SOURCE_FILES})

target_include_directories(${PROJECT_NAME} PRIVATE ${EASYX_INCLUDE})

# ========== 告诉链接器去哪个目录找库 ==========
target_link_directories(${PROJECT_NAME} PRIVATE
    "${EASYX_LOCAL_PATH}/lib/x64"
    "C:/Program Files (x86)/EasyX/lib/x64"
    "C:/EasyX/lib/x64"
)

# ========== UNICODE ==========
target_compile_definitions(${PROJECT_NAME} PRIVATE UNICODE _UNICODE)

# ========== vcpkg 库查找 ==========
list(APPEND CMAKE_PREFIX_PATH "D:/SOFT/vcpkg/installed/x64-windows")
find_package(CURL CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# ========== 链接所有库（系统库 + vcpkg 库） ==========
target_link_libraries(${PROJECT_NAME} PRIVATE
    gdi32
    ole32
    oleaut32
    winmm
    msimg32
    CURL::libcurl
    nlohmann_json::nlohmann_json
)
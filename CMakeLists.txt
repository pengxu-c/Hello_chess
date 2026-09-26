cmake_minimum_required(VERSION 3.15)
project(Hello_chess)
set(X_VCPKG_APPLOCAL_DEPS_INSTALL ON)
set(CMAKE_CXX_STANDARD 17)
if(MSVC)
    add_compile_options(/utf-8)
else()
    add_compile_options(-finput-charset=UTF-8 -fexec-charset=GBK)
endif()

# ========== 源文件 ==========
set(SOURCE_FILES
    main.cpp
    core.cpp
    ui.cpp
    player.cpp
    threat.cpp
    ai_config.cpp
    ai_player.cpp
    controller.cpp
    storage.cpp
    experimental_tactical_player.cpp
    server/asset_provider.cpp
    server/browser_launcher.cpp
    server/game_server.cpp
    server/session.cpp
)

add_executable(${PROJECT_NAME} ${SOURCE_FILES})

# 编译为 Windows 子系统：双击运行不出现控制台窗口。
# --cli 模式由 main.cpp 的 attachConsole() 按需附加控制台。
set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)

# 项目根目录（使 #include "server/xxx.h" 生效）与第三方单头文件目录（cpp-httplib）
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

# ========== UNICODE ==========
target_compile_definitions(${PROJECT_NAME} PRIVATE UNICODE _UNICODE)

# ========== vcpkg 库查找 ==========
list(APPEND CMAKE_PREFIX_PATH "D:/SOFT/vcpkg/installed/x64-windows")
find_package(CURL CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# ========== 链接库 ==========
# ws2_32：cpp-httplib 在 Windows 下依赖 Winsock
# EasyX 已移除，不再需要 gdi32/msimg32 等图形库
target_link_libraries(${PROJECT_NAME} PRIVATE
    ws2_32
    CURL::libcurl
    nlohmann_json::nlohmann_json
)
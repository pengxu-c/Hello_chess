cmake_minimum_required(VERSION 3.15)
project(Hello_chess)
set(X_VCPKG_APPLOCAL_DEPS_INSTALL ON)
set(CMAKE_CXX_STANDARD 17)
if(MSVC)
    add_compile_options(/utf-8)
else()
    add_compile_options(-finput-charset=UTF-8 -fexec-charset=GBK)
endif()

# ========== vcpkg 库查找 ==========
list(APPEND CMAKE_PREFIX_PATH "D:/SOFT/vcpkg/installed/x64-windows")
find_package(CURL CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# ============================================================================
#  分层（依赖只能从上往下，绝不允许反向）：
#     contracts/  纯接口与数据类型，零实现依赖
#     kernel/     棋规内核（棋盘/规则/状态机/会话编排/存储边界）
#     app/        协议转换（JSON）、参数解析、界面主循环驱动
#     plugins/    插件：players（玩家）、views（界面）、storage（存储）
#     hosts/      宿主：cli（终端）、web（HTTP）、selfplay（无头）、selftest（自检）
#
#  【加一个玩家】plugins/players/*.h/.cpp + plugins/PlayerRegistry.cpp 一行
#  【加一个界面】plugins/views/*.h/.cpp   + plugins/ViewRegistry.cpp   一行
#  【加一个存储】plugins/storage/*.cpp    + hosts 里换掉工厂函数
#  三种扩展都不需要改 kernel / contracts。
# ============================================================================
set(GOMOKU_CORE_SOURCES
    # ---- 内核 ----
    kernel/SquareBoard.cpp
    kernel/GomokuRules.cpp
    kernel/MatchState.cpp
    kernel/MatchSession.cpp
    kernel/NullStorageGateway.cpp

    # ---- 协议 / 参数 ----
    app/JsonProtocol.cpp
    app/Options.cpp
    app/ResourcePaths.cpp

    # ---- 插件：玩家 ----
    plugins/PlayerRegistry.cpp
    plugins/players/NativePlayers.cpp
    plugins/players/EnginePlayers.cpp

    # ---- 插件：界面 ----
    plugins/ViewRegistry.cpp
    plugins/views/CliView.cpp
    plugins/views/WebView.cpp

    # ---- 插件：存储 ----
    plugins/storage/LegacyStorageGateway.cpp

    # ---- 宿主 ----
    hosts/web/HttpServer.cpp
    hosts/web/BrowserLauncher.cpp
    hosts/CliHost.cpp
    hosts/WebHost.cpp
    hosts/SelfPlayHost.cpp
    hosts/SelfTestHost.cpp

    # ---- 入口 ----
    main_microkernel.cpp

    # ---- 复用的既有 AI 引擎（源码不动，通过 plugins/players/EnginePlayers 接入） ----
    engine/core.cpp
    engine/player.cpp
    engine/threat.cpp
    engine/tactical_max.cpp
    engine/ai_config.cpp
    engine/ai_player.cpp
    engine/storage.cpp
)

add_executable(Gomoku ${GOMOKU_CORE_SOURCES})

# 【为什么用控制台子系统，而不是 Windows 子系统】
#   实测：Windows 子系统（WIN32_EXECUTABLE TRUE）下，--list / --selfplay 这类
#   "打印后退出"的模式输出为空、退出码 0xC0000005，重定向到文件也是空文件。
#   控制台子系统下同样命令一切正常，且可以被 CI 断言。
#   于是这里用控制台子系统，并由 main_microkernel.cpp 在 Web 模式下主动隐藏
#   控制台窗口，达到"双击不弹黑框"的相同体验（见 hideConsoleWindowIfOwned()）。
set_target_properties(Gomoku PROPERTIES WIN32_EXECUTABLE FALSE)

target_include_directories(Gomoku PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_compile_definitions(Gomoku PRIVATE UNICODE _UNICODE)

# 源码根锚点：编译期把"源码在哪"固化进 exe。
# 有了它，运行时定位 webapp/ 与 contracts/ 不再依赖当前工作目录，
# exe 被拷到任何位置、从任何目录启动都能正确找到资源。
target_compile_definitions(Gomoku PRIVATE
    GOMOKU_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

# ws2_32：cpp-httplib 在 Windows 下依赖 Winsock
target_link_libraries(Gomoku PRIVATE
    ws2_32
    CURL::libcurl
    nlohmann_json::nlohmann_json
)

# ============================================================================
#  ★ GomokuBench —— 棋力 / 正确性回归基准
#
#  只依赖契约层 + 内核 + 玩家插件（本地引擎），
#  刻意不链接 curl / nlohmann_json / iphlpapi / CSRSS 服务器 / 界面：
#    * 因此可以随时在 CI 或本地命令行重跑；
#    * 6 号（API AI）有网络依赖，注册在 plugins/PlayerRegistry.cpp —— 那个文件
#      被 GOMOKU_NO_API_PLAYER 编译期排除，于是 bench 完全不碰网络栈。
# ============================================================================
set(BENCH_SOURCE_FILES
    bench/bench_main.cpp
    bench/bench_match.cpp
    bench/bench_selftest.cpp

    # 契约 + 内核（与产品同一份代码）
    kernel/SquareBoard.cpp
    kernel/GomokuRules.cpp
    kernel/MatchState.cpp
    kernel/NullStorageGateway.cpp

    # 玩家插件（含旧引擎适配器）
    plugins/players/NativePlayers.cpp
    plugins/players/EnginePlayers.cpp
    plugins/PlayerRegistry.cpp

    # 既有 AI 引擎
    engine/core.cpp
    engine/player.cpp
    engine/threat.cpp
    engine/tactical_max.cpp
)
add_executable(GomokuBench ${BENCH_SOURCE_FILES})
target_include_directories(GomokuBench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
# bench 需要占用控制台输出结果
set_target_properties(GomokuBench PROPERTIES WIN32_EXECUTABLE FALSE)
# 编译期关掉 API 玩家注册：该插件依赖 libcurl，bench 不链接网络栈
target_compile_definitions(GomokuBench PRIVATE GOMOKU_NO_API_PLAYER)

# ============================================================================
#  构建后部署：把 exe 真正需要的运行时文件放到它旁边。
#
#  【为什么必须做】
#    vcpkg 的 libcurl.lib 只有 22KB —— 它是"导入库"，真正的实现是
#    libcurl.dll。若不把 DLL 拷到 exe 同目录，程序在"双击运行"时会因为
#    找不到依赖而无法启动（而在开发机的 PATH 里恰好能找到，所以问题
#    只在交付给用户时才暴露 —— 最难排查的那类坑）。
#    webapp/ 同理：前端资源必须是 exe 的邻居。
# ============================================================================

# 从 CURL::libcurl 这个 imported target 拿到真实的 .lib 路径，再反推 DLL 目录。
# 【为什么不用 CURL_LIBRARY_RELEASE】那个变量并非所有 CURLConfig.cmake 都会导出，
# 换一台机器/换一版 vcpkg 就可能为空 —— 那样 DLL 部署会静默失效。
# IMPORTED_LOCATION 是 imported target 的标准属性，取它更稳。
get_target_property(_curl_imported CURL::libcurl IMPORTED_LOCATION_RELEASE)
if(NOT _curl_imported)
    get_target_property(_curl_imported CURL::libcurl IMPORTED_LOCATION)
endif()

set(GOMOKU_RUNTIME_DLLS)
if(_curl_imported)
    get_filename_component(_curl_lib_dir "${_curl_imported}" DIRECTORY)
    # vcpkg 布局： lib/ 与 bin/ 是兄弟目录
    get_filename_component(_curl_bin_dir "${_curl_lib_dir}/../bin" ABSOLUTE)
    if(EXISTS "${_curl_bin_dir}/libcurl.dll")
        # libcurl 的其它依赖（zlib 等）一并带上，缺一个就整体起不来
        file(GLOB _curl_deps "${_curl_bin_dir}/*.dll")
        list(APPEND GOMOKU_RUNTIME_DLLS ${_curl_deps})
        list(REMOVE_DUPLICATES GOMOKU_RUNTIME_DLLS)
    else()
        message(STATUS "libcurl.dll not found near ${_curl_lib_dir}; "
                       "runtime DLLs will not be auto-copied")
    endif()
endif()

add_custom_command(TARGET Gomoku POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/webapp
            $<TARGET_FILE_DIR:Gomoku>/webapp
    COMMENT "Copying webapp/ next to Gomoku.exe"
)

if(GOMOKU_RUNTIME_DLLS)
    add_custom_command(TARGET Gomoku POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${GOMOKU_RUNTIME_DLLS} $<TARGET_FILE_DIR:Gomoku>
        COMMENT "Copying runtime DLLs (libcurl etc.) next to Gomoku.exe"
    )
endif()

cmake_minimum_required(VERSION 3.15)
# 工程名 = 输出 exe 名（用户要求）。
# 注意：CMake 默认输出 ${PROJECT_NAME}.exe，所以改名后
# 下面那行 OUTPUT_NAME 就成了冗余，但保留无害且更明确。
project(hello_chess)
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

    # ---- EasyX 界面不在这里，见下方 if(EASYX_FOUND) 分支 ----
    # 原因：它需要 easyx.h 与 EasyXw.lib，且 easyx.h 用 #pragma comment(lib)
    # 把静态库强行拉进本目标。找不到头文件时若仍参与编译，整个 hello_chess
    # 会直接失败（旧注释说的"不影响其它目标"并不成立）。改为条件加入。

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

# ============================================================================
#  EasyX 图形窗口界面（界面插件，--view easyx）
#
#  【为什么只加 include 目录、不加 link_libraries】
#  EasyX 被安装在 MSVC 的集成目录里，它的 easyx.h 内部写着：
#      #pragma comment(lib, "EasyXw.lib")
#      #pragma comment(lib, "EasyXa.lib")
#  这是 MSVC 特有指令，链接器会自动去对应目录找这两个 .lib。
#  所以只要头文件能被找到，链接就自动完成 —— 这正是 EasyX
#  "不需要配置链接器" 的由来。
#
#  【路径里有空格 / VS 换版本】
#  旧路径 "Visual studio" 含空格，CMake 里必须加引号，否则会被拆成两个参数。
#  2026-11 本机 VS 升级到 18 2026、安装目录改为 D:/visualstudio，旧实例连同
#  集成进去的 easyx.h 一起消失。EasyX 需要随新 VS 重新安装一次；在此之前
#  可用 EASYX_LOCAL_PATH 指向独立安装目录，或 -DGOMOKU_WITH_EASYX=OFF 关掉插件。
#  路径不存在时不会报错：只是该界面插件不参与编译，其余目标不受影响。
# ============================================================================
set(EASYX_INCLUDE_DIR "$ENV{EASYX_LOCAL_PATH}/include"
    "D:/visualstudio/VC/Auxiliary/VS/include"
    "D:/Visual studio/VS2022/VC/Auxiliary/VS/include"
    "C:/Program Files (x86)/EasyX/include"
    "C:/EasyX/include"
)
# 是否编译 EasyX 界面插件。没有装 EasyX 的机器上用 -DGOMOKU_WITH_EASYX=OFF 关闭。
option(GOMOKU_WITH_EASYX "Build the EasyX view plugin (requires easyx.h)" ON)

set(EASYX_FOUND "")

foreach(dir IN LISTS EASYX_INCLUDE_DIR)
    if(EXISTS "${dir}/easyx.h")
        set(EASYX_FOUND "${dir}")
        break()
    endif()
endforeach()
if(EASYX_FOUND)
    message(STATUS "EasyX headers found: ${EASYX_FOUND}")
else()
    message(STATUS "easyx.h not found: the easyx view plugin will not be built; "
                   "everything else still works (set EASYX_LOCAL_PATH to point at it)")
endif()

add_executable(hello_chess ${GOMOKU_CORE_SOURCES})

# ============================================================================
#  输出文件名：Gomoku.exe
#
#  【为什么目标名仍然叫 hello_chess】
#   目标名只在构建期可见（.sln / .vcxproj 的文件名），改它要重新 configure；
#   交付名由 OUTPUT_NAME 决定，改这一处就够。
#
#  【历史坑】README 一直按 Gomoku.exe 写，而这里曾经输出
#  hello_chess.exe，于是照着文档找程序的人找不到东西。现在以文档为准，
#  统一成 Gomoku.exe —— 文档与产物只能有一个是对的。
# ============================================================================
set_target_properties(hello_chess PROPERTIES OUTPUT_NAME "Gomoku")

# 【为什么用控制台子系统，而不是 Windows 子系统】
#   实测：Windows 子系统（WIN32_EXECUTABLE TRUE）下，--list / --selfplay 这类
#   "打印后退出"的模式输出为空、退出码 0xC0000005，重定向到文件也是空文件。
#   控制台子系统下同样命令一切正常，且可以被 CI 断言。
#   控制台一律保留可见（用户明确要求）：关掉控制台窗口就是退出程序。
set_target_properties(hello_chess PROPERTIES WIN32_EXECUTABLE FALSE)

target_include_directories(hello_chess PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

# EasyX 头文件（easyx.h 内部用 #pragma comment(lib) 自动链接 EasyXw/EasyXa，
# 因此这里只给头文件路径即可，不要 target_link_libraries）。
# 见上文"EasyX 图形窗口界面"一节对 EASYX_FOUND 的说明。
if(EASYX_FOUND AND GOMOKU_WITH_EASYX)
    target_include_directories(hello_chess PRIVATE ${EASYX_FOUND})
    # EasyX 界面插件只在能编译时加入，并用宏告诉 ViewRegistry 是否注册它。
    target_sources(hello_chess PRIVATE
        plugins/views/EasyXView.cpp
        engine/legacy_easyx/ui.cpp
    )
    target_compile_definitions(hello_chess PRIVATE GOMOKU_HAS_EASYX=1)
endif()

target_compile_definitions(hello_chess PRIVATE UNICODE _UNICODE)

# 源码根锚点：编译期把"源码在哪"固化进 exe。
# 有了它，运行时定位 webapp/ 与 contracts/ 不再依赖当前工作目录，
# exe 被拷到任何位置、从任何目录启动都能正确找到资源。
target_compile_definitions(hello_chess PRIVATE
    GOMOKU_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

# ws2_32：cpp-httplib 在 Windows 下依赖 Winsock
target_link_libraries(hello_chess PRIVATE
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

add_custom_command(TARGET hello_chess POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/webapp
            $<TARGET_FILE_DIR:hello_chess>/webapp
    COMMENT "Copying webapp/ next to Gomoku.exe"
)

if(GOMOKU_RUNTIME_DLLS)
    add_custom_command(TARGET hello_chess POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${GOMOKU_RUNTIME_DLLS} $<TARGET_FILE_DIR:hello_chess>
        COMMENT "Copying runtime DLLs (libcurl etc.) next to Gomoku.exe"
    )
endif()

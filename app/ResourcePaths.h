// ============================================================
// app/ResourcePaths.h - 运行时资源定位（前端目录 / 源码根）
//
// 【为什么需要它】
//   同一个 exe 会被从不同工作目录启动：
//     build/Release/Gomoku.exe   —— CMake 的正常产物
//     chess/Gomoku.exe           —— 手工拷过去双击
//     任意目录                   —— CI 里绝对路径调用
//   凡是"按相对路径找文件"的地方（webapp/、contracts/），
//   都会随工作目录漂移。之前 WebHost 与自检各写了一套探测逻辑，
//   结果是同一份代码在 build 目录下跑必然自检失败 ——
//   那是"我的启动方式不对"还是"产品坏了"，没人分得清。
//
//   定位顺序统一为：编译期源码目录 → exe 同级 → exe 上溯若干级 → 当前目录。
//   显式传入的路径永远优先，方便测试与自定义部署。
// ============================================================
#pragma once
#include <string>

namespace gomoku {

// exe 所在目录（末尾无分隔符）。取不到时返回 "."。
std::string executableDir();

// 定位前端资源目录（内含 index.html）。
// 显式 root 非空时直接返回它（不校验，由调用方负责）。
// 找不到返回空字符串 —— 调用方必须处理，不要静默用一个错的路径。
std::string resolveWebRoot(const std::string& explicitRoot = std::string());

// 定位源码根（同时含 contracts/ 与 kernel/）。
// 供分层守卫扫源码用；发布产物里通常不存在，此时返回空字符串，
// 调用方应据此"跳过"而不是"判失败"。
std::string resolveSourceRoot();

}  // namespace gomoku

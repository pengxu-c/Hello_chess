// ============================================================
// main.cpp - 程序入口
// 仅构造 GameController 并启动，所有逻辑均在类中
// ============================================================
#include "controller.h"
#include <cstdio>

int main() {
    GameController gc;
    gc.run();
    // 退出前暂停：双击 exe 运行时进程结束会立即关闭控制台窗口，
    // 导致最后的输出来不及查看。等待用户按回车后再退出，
    // 有残留输入行时先消费掉（避免暂停被旧输入跳过）。
    printf("\nPress Enter to exit...");
    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF) {}
    return 0;
}

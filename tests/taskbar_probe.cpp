#include "platform/taskbar.hpp"
#include <iostream>
int main(int argc, char **argv) {
    using namespace wfx;
    Recovery recovery;
    if (!recovery.start())
        return 1;
    HWND h = FindWindowW(L"Shell_TrayWnd", nullptr);
    const auto original = capture(h, Mode::opacity);
    if (!original || !inspect_taskbar(h))
        return 2;
    Taskbar taskbar(recovery);
    std::cout << taskbar.apply(0) << '\n';
    Sleep(3500);
    const auto current = capture(h, Mode::opacity);
    const bool alpha_unchanged = current && state_matches(*current, *original, Mode::opacity);
    const bool running = taskbar.running();
    if (argc > 1 && std::string(argv[1]) == "--crash-controller") {
        std::cout << "Registered companion running: " << running << ", alpha unchanged: " << alpha_unchanged
                  << std::endl;
        TerminateProcess(GetCurrentProcess(), running && alpha_unchanged ? 0 : 3);
    }
    taskbar.restore();
    for (int i = 0; i < 60; ++i) {
        taskbar.poll();
        Sleep(100);
    }
    std::cout << "Companion running: " << running
              << "\nTaskbar/icon window alpha unchanged: " << alpha_unchanged
              << "\nBackground visible coverage requires an interactive check.\n";
    return running && alpha_unchanged ? 0 : 3;
}

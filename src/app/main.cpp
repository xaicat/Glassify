#include "app/controller.hpp"
#include <string>
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int show) {
    std::wstring arguments = command ? command : L"";
    const auto first = arguments.find_first_not_of(L" \t\r\n");
    arguments = first == std::wstring::npos
                    ? L""
                    : arguments.substr(first, arguments.find_last_not_of(L" \t\r\n") - first + 1);
    return wfx::ui::run_application(instance, arguments.data(), show);
}

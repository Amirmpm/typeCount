#include "app.hpp"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR lpCmdLine, int) {
    tc::win::init_per_monitor_dpi();
    tc::win::TypeCountApp app;
    return app.run(hInstance, lpCmdLine ? lpCmdLine : L"");
}

#include "app.hpp"
#include <X11/Xlib.h>

int main(int argc, char** argv) {
    XInitThreads();
    tc::linux_host::TypeCountApp app;
    return app.run(argc, argv);
}

#include <AYTest.h>
#include <AYGameLoop.h>

int main() {
    int rc = ayt::test::runAllTests("AYGameLoop");
    // Shut down GameLoop BEFORE main() returns so the static dtor
    // doesn't fire after AYLog / AYEventSystem have already torn down
    // (which would UAF inside ayt::log::info()).
    ayt::game::GameLoop::instance().shutdown();
    return rc;
}
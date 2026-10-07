#include <AYTest.h>
#include <AYGameLoop.h>

int main(int argc, char** argv) {
    int rc = ayt::test::runTests("AYGameLoop", argc, argv);
    // Shut down GameLoop BEFORE main() returns so the static dtor
    // doesn't fire after AYLog / AYEventSystem have already torn down
    // (which would UAF inside ayt::log::info()).
    ayt::game::GameLoop::instance().shutdown();
    return rc;
}

#include "audio/Engine.h"
#include "audio/EngineFactories.h"

std::unique_ptr<IEngine> createEngine() {
#ifdef WNAMP_HAVE_GSTREAMER
    if (auto e = createGStreamerEngine()) return e;
#endif
    return createNullEngine();
}

#pragma once
#include <memory>
#include "audio/IEngine.h"
std::unique_ptr<IEngine> createNullEngine();
#ifdef WNAMP_HAVE_GSTREAMER
std::unique_ptr<IEngine> createGStreamerEngine();
#endif

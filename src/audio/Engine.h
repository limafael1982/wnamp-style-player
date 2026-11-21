#pragma once
#include <memory>
#include "audio/IEngine.h"

// Returns the GStreamer engine when built with it, otherwise a silent stub.
std::unique_ptr<IEngine> createEngine();

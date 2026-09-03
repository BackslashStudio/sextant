#pragma once

// Umbrella header -- include this in client code. It pulls in the whole public
// API and declares nothing of its own. The API is entirely object-oriented:
// every operation is a member of Figure or Axes, reached from
// Figure::create(). There is no module-level layer writing into a hidden
// current-figure global.

#include "export.h"
#include "style.h"
#include "axes.h"
#include "figure.h"

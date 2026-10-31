#pragma once

// Umbrella header -- include this in client code. It pulls in the whole public
// API and declares nothing of its own. The core API is object-oriented: every
// plot of a fundamental kind is a member of Figure or Axes, reached from
// Figure::create(). Plots derived from those (hist(), hist2d(), imshow()) are
// free functions in derived.h, taking the Axes they draw into. There is no
// module-level layer writing into a hidden current-figure global.

#include "export.h"
#include "style.h"
#include "axes.h"
#include "polar_axes.h"
#include "figure.h"
#include "derived.h"

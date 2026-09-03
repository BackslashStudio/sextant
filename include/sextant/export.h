#pragma once

#if defined(SEXTANT_STATIC_DEFINE)
  #define SEXTANT_API
#elif defined(_WIN32)
  #ifdef SEXTANT_BUILD_SHARED
    #define SEXTANT_API __declspec(dllexport)
  #else
    #define SEXTANT_API __declspec(dllimport)
  #endif
#else
  #define SEXTANT_API __attribute__((visibility("default")))
#endif

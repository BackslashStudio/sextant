/* Force-included (-include) into every source of the bundled FreeType, off
 * MSVC. FreeType marks its API visibility("default") unconditionally for
 * GCC/Clang, which -fvisibility=hidden does not override, so a shared object
 * linking the archive -- libsextant, or a Python extension module linking
 * sextant_static -- would export all of FT_*. Including public-macros.h first
 * claims its include guard; the definitions below then stand for the whole
 * translation unit, and the preset hides the API like everything else.
 * (CMakeLists.txt, SEXTANT_BUNDLE_DEPS.) */
#include <freetype/config/public-macros.h>

#undef FT_PUBLIC_FUNCTION_ATTRIBUTE
#define FT_PUBLIC_FUNCTION_ATTRIBUTE /* empty */

#undef FT_EXPORT
#define FT_EXPORT(x) extern x

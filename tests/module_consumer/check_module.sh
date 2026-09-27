#!/usr/bin/env bash
# What a module linking sextant_static depends on and exports.
#
#   check_module.sh <module>
#
# Fails if it needs a FreeType, libpng, zlib, GLFW or sextant binary at run time
# -- they are meant to be inside it -- or exports a symbol of theirs, or of
# ImGui/NanoVG/GLAD: those must stay private to the module, since another one in
# the same process may carry its own copies. Linux uses readelf/nm, macOS
# otool/nm, Windows (Git Bash) dumpbin from an MSVC environment.
set -uo pipefail

module=${1:?usage: check_module.sh <module>}
[ -f "$module" ] || { echo "no such file: $module"; exit 2; }

case "$(uname -s)" in
    Linux)
        deps=$(readelf -d "$module" | awk '/\(NEEDED\)/ { gsub(/[\[\]]/, "", $NF); print $NF }')
        exports=$(nm -D --defined-only -C "$module" | cut -d' ' -f3-)
        ;;
    Darwin)
        # First line is the module itself.
        deps=$(otool -L "$module" | awk 'NR > 1 { print $1 }')
        exports=$(nm -gU "$module" | cut -d' ' -f3- | c++filt)
        ;;
    MINGW*|MSYS*|CYGWIN*)
        export MSYS_NO_PATHCONV=1
        deps=$(dumpbin /nologo /dependents "$module" \
               | awk '/following dependencies/ { on = 1; next } on && /^ *$/ { if (seen) exit; next }
                      on { seen = 1; print $1 }')
        exports=$(dumpbin /nologo /exports "$module" \
                  | awk '/ordinal +hint +RVA +name/ { on = 1; next } on && NF >= 4 { print $4 }')
        ;;
    *)
        echo "unsupported platform: $(uname -s)"; exit 2
        ;;
esac

# The OS's own are fine by name: macOS's libz is in /usr/lib, as on every Mac,
# and is meant to be linked from there.
bad_deps=$(printf '%s\n' "$deps" | grep -vE '^(/usr/lib/|/System/)' \
           | grep -iE 'freetype|png|(^|/)libz\.|zlib|glfw|sextant' || true)
bad_exports=$(printf '%s\n' "$exports" \
    | grep -E 'sextant::|glfw|(^|[^A-Za-z])_?FT_|(^|[^A-Za-z])_?png_|ImGui|(^|[^A-Za-z])_?nvg|glad|^_?(deflate|inflate|adler32|crc32|zlibVersion|compress|uncompress|gz[a-z]+)' \
    || true)

echo "module: $module"
echo "run-time dependencies:"
printf '  %s\n' $deps
echo "exported symbols: $(printf '%s\n' "$exports" | grep -c . || true)"
printf '%s\n' "$exports" | grep . | head -n 20 | sed 's/^/  /'

status=0
if [ -n "$bad_deps" ]; then
    echo "FAIL: depends on what should be linked into it:"
    printf '  %s\n' $bad_deps
    status=1
fi
if [ -n "$bad_exports" ]; then
    echo "FAIL: exports what should stay private to it ($(printf '%s\n' "$bad_exports" | wc -l) symbols):"
    printf '%s\n' "$bad_exports" | head -n 40 | sed 's/^/  /'
    status=1
fi
[ $status -eq 0 ] && echo "OK"
exit $status

# rapidyaml (vendored)

[rapidyaml](https://github.com/biojppm/rapidyaml) v0.16.0, the single-source release (MIT, see
`LICENSE.txt`). It is private to the library: only `src/yaml_io.cpp` includes it, behind the small
`sextant::yaml` value tree in `src/yaml_io.h`; no public header and no installed header mentions it.

- `ryml_singlesrc.hpp` / `ryml_singlesrc.cpp` are the release's `rapidyaml.v0.16.0.singlesrc.*`
  files, renamed. They are fetched by `scripts/setup_deps.sh` / `setup_deps.bat`.
- **One change**, line 46 of `ryml_singlesrc.cpp`: the release includes its header by an absolute
  path of the CI machine that built it (`#include </home/runner/work/.../rapidyaml.v0.16.0.singlesrc.hpp>`);
  it is now `#include "ryml_singlesrc.hpp"`. The setup scripts apply the same edit.
- The `.cpp` is compiled as its own translation unit with warnings off (`CMakeLists.txt`,
  `RYML_SOURCE`); the code is not ours to fix.
- To update: change the version in both setup scripts and this file, re-fetch, and run
  `sextant_layout_test` (`yaml_io.cpp` there exercises parse, emit, round trips and errors).

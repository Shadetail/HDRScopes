# HDRScopes — project notes

Read `docs/ARCHITECTURE.md` first for the pipeline, build commands, and the
runtime-shader dev loop (edit `.hlsl`, relaunch, no rebuild).

- `reference/` is local-only (gitignored): manual excerpts, cloned repos,
  planning notes, retired v1 code. Never commit anything from it.
- Release flow: bump `VERSION` in CMakeLists.txt, tag `vx.y.z`, push the tag.
- User settings live in `%LOCALAPPDATA%\HDRScopes\settings.ini` — back them up
  before test-launching with modified settings, and restore after.

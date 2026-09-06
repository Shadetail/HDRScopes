# HDRScopes — project notes

Read `docs/ARCHITECTURE.md` first for the pipeline, build commands, and the
runtime-shader dev loop (edit `.hlsl`, relaunch, no rebuild).

- `reference/` is local-only (gitignored): manual excerpts, cloned repos,
  planning notes, retired v1 code. Never commit anything from it.
- Release flow: bump `VERSION` in CMakeLists.txt, add a `## vx.y.z` section to
  CHANGELOG.md (it becomes the GitHub Release body), tag `vx.y.z`, push the tag.
  Before announcing a release, scan the published zip on VirusTotal and link
  the report in the release notes (a fresh unsigned exe starts with zero
  reputation; v1.0.6 was a Defender false positive). Free code signing via
  SignPath Foundation is an option on file, deliberately not started — see
  `reference/planning/signpath-application.md` (revisit if a release is
  flagged again).
- User settings are local-first: `settings.ini` next to the exe (for dev runs
  that means `build/Release/settings.ini`), falling back to
  `%LOCALAPPDATA%\HDRScopes\settings.ini` (legacy/non-writable installs).
  Back up whichever exists before test-launching with modified settings, and
  restore after. The app saves only on clean exit, not on force-kill.

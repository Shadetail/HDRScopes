# Changelog

User-facing changes per release. When a `vX.Y.Z` tag is pushed, the release
workflow copies the matching `## vX.Y.Z` section into the GitHub Release body,
so every release ships with a readable summary.

## v1.0.3 — 2026-08-16

- 4-scope layout: when one column holds only the square scopes (vectorscope,
  CIE) and the other only the stretchy ones (waveform, histogram), the
  vertical split now shifts automatically so the square column is exactly as
  wide as its graphs can use — the waveform/histogram column gets all the
  reclaimed width instead of leaving dead space beside the square scopes.
- Fixed: in the 2- and 4-scope layouts, the scope-type dropdowns no longer
  overlap the waveform's nit labels — they now start right of the label column.
- Scope-type dropdowns size themselves to the selected scope's name, so a
  short name like "Waveform" no longer blocks the graph with unused width.
- The floating controls (scope pickers and the top-right button strip) fade
  out while the mouse is outside the HDRScopes window, keeping the scopes
  unobstructed when you're just watching. A new Controls > Preferences >
  "Idle controls opacity" slider sets how visible they stay (default 5%,
  100% = never fade).
- GitHub Releases now include a summarized changelog (this file) instead of
  only the commit-diff link.

## v1.0.2 — 2026-08-16

- Per-monitor DPI scaling for the whole UI: fonts, widgets, and scope label
  margins follow the window's monitor scale and rescale live when the window
  moves between monitors (loads Consolas at scaled sizes above 1x).

## v1.0.1 — 2026-08-16

- Settings are now local-first: `settings.ini` lives next to the exe, making
  an unzipped copy fully portable. `%LOCALAPPDATA%\HDRScopes` remains the
  fallback for non-writable install folders (e.g. Program Files), and a
  pre-1.0.1 file there still loads and migrates on first save.

## v1.0.0 — 2026-08-16

- First public release: waveform, histogram, vectorscope, and CIE
  chromaticity scopes for the live Windows HDR (scRGB) signal, with 1/2/4-up
  layouts, monitor/window/rect capture, hover probe markers, nit readouts,
  PQ-encoded vectorscope with SDR primary markers, and configurable
  reference lines.

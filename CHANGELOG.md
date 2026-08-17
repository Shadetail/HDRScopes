# Changelog

User-facing changes per release. When a `vX.Y.Z` tag is pushed, the release
workflow copies the matching `## vX.Y.Z` section into the GitHub Release body,
so every release ships with a readable summary.

## v1.0.6 — 2026-08-17

- Window mode now shows which window is actually being scoped (thanks to
  DatTestBench for the request): the title bar appends the target window's
  title while it is being scoped, and the Capture section shows a
  "Scoping: ..." confirmation under the picker button. When nothing is
  cropped — no window picked yet, the window closed, or it sits wholly off
  the captured monitor — both readouts say so instead of naming a window
  that isn't being scoped.
- Fixed: after a scoped window closed, Windows could reuse its window handle
  for an unrelated window, silently switching the scopes to that window. The
  pick is now tied to the original window's process and lapses cleanly to
  the full monitor instead.

## v1.0.5 — 2026-08-16

- Update check: on startup the app now asks github.com whether a newer
  release exists and, if so, shows a small notice with View release / Skip
  this version / Later. Only the latest version number is fetched — nothing
  is sent beyond the request itself and nothing installs automatically. Can
  be turned off under Controls > Preferences > "Check for updates at
  startup".

## v1.0.4 — 2026-08-16

- The title bar is now always dark to match the app's dark theme. Previously
  it followed legacy Windows behavior and turned white whenever the window
  lost focus — which is most of the time for a scopes app you watch while
  working elsewhere.

## v1.0.3 — 2026-08-16

- CIE: the spectral-locus horseshoe is drawn as a smooth spline through the
  5 nm table instead of showing straight chords on its fast-sweeping
  cyan-green edge.
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

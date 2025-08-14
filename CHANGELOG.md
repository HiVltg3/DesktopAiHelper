# Changelog
## v0.3.1 — 2025-08-14
### Added
- **Translate** button in the rewrite panel. Defaults to English and overwrites source text.
- Optional target language input (commented code in `onTranslateButtonClicked`; can be replaced with a ComboBox).

## v0.3.0 — 2025-08-14
### Added
- Focused-element targeting + broadened control types (Edit/Document/Text)
- Panel follow with own-process filtering (prevents drift)

### Fixed
- Re-entrancy/floods (in-flight gate + paste gate)
- “Only shows once” (state reset + dedupe clearing)
- Empty-send (do not clear pending on hide; copy-before-hide)
- Text read fallback (ValuePattern → TextPattern; no abort on rect failure)
- UIA lifetime and condition releases

## v0.2.0 — 2025-08-12
### Fixed
- Prevent re-entrancy from simulated paste (ignoreHook + in-flight gate)
- Persistent panel with proper state reset; mode-switch no longer blocks popups
- UIA element lifetime fixes (editBlock AddRef/Release order)

### Known
- Some rich text controls still rely on fallback paste


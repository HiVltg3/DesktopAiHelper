# Architecture & Threading

```
[Keyboard Hook Thread]
   │ (WM_KEYUP)
   │ ├─ if (g_ignoreHook) → pass through (no capture)
   │ └─ if (!g_captureInFlight.exchange(true)) → invokeMethod(..., Qt::QueuedConnection)
   ▼
[Qt GUI Thread / Main]
   ├─ RAII guard: on function exit → g_captureInFlight = false
   ├─ UIA target selection:
   │     • GetFocusedElement() first (accurate in browsers)
   │     • else ElementFromHandle(hwnd)
   │     • if target doesn't expose text, FindAll(Edit|Document|Text)
   ├─ Read:
   │     • try ValuePattern::get_CurrentValue()
   │     • if empty/unsupported → TextPattern::get_DocumentRange()->GetText(-1)
   ├─ UI:
   │     • processCapturedText → debounce 800ms → showRewriteUI(...)
   │     • when panel visible: update cached text but don't re-popup
   └─ Write-back:
         • try ValuePattern::SetValue()
         • else fallback Ctrl+A / Ctrl+V (g_ignoreHook=true during paste)
```

## Design principles

- **Queue heavy work to main thread**: COM/UI work inside the hook can stall input.
- **Avoid re-entrancy**: Guard paste with `g_ignoreHook` to ignore fake key events.
- **Flood control**: `g_captureInFlight` ensures only one capture runs at a time.
- **Always reset**: RAII guard sets `g_captureInFlight=false` on every exit path.
- **Target accuracy**: `GetFocusedElement()` for Chromium-based apps; broaden control types to Edit/Document/Text.
- **Panel follow**: a timer refreshes position based on focused element’s rect; **ignore own-process elements** (via `UIA_ProcessIdPropertyId` and `UIA_NativeWindowHandlePropertyId`) to prevent drift.

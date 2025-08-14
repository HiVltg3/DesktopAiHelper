# Debug Log (Root Cause Analyses)

This documents the issues we solved and the exact changes we made.

---

## 1) Hook re-entrancy & floods (freeze/crash)
**Symptom:** After a few rewrites, input stalls or crashes.  
**Root cause:** In-flight condition inverted; paste simulated keys re-triggered capture.  
**Fix:**
- Use `g_captureInFlight.exchange(true)` and **only** enqueue when it returns `false`.
- Guard paste with `g_ignoreHook=true`; reset on **every** path (including early returns).  
**Patch (core):**
```cpp
bool already = g_captureInFlight.exchange(true, std::memory_order_acq_rel);
if (!already) {
  HWND h = hWndFocus;
  QMetaObject::invokeMethod(g_pMainWindow,[h](){
    struct Guard{ ~Guard(){ g_captureInFlight.store(false, std::memory_order_release);} } _g;
    g_pMainWindow->getControlTextUIAInternal(h);
  }, Qt::QueuedConnection);
}
```
```cpp
g_ignoreHook.store(true, std::memory_order_release);
HWND hwndFocus = GetFocus();
if (hwndFocus == NULL) { g_ignoreHook.store(false, std::memory_order_release); return; }
// simulate Ctrl+A / Ctrl+V ...
QTimer::singleShot(150, this, [](){ g_ignoreHook.store(false, std::memory_order_release); });
```

---

## 2) “Only shows once”
**Root cause:** `isRewriteFlowActive` not reset on hide; `lastCapturedText` blocked dedupe.  
**Fix:** Persistent panel (show/hide/move), `hideRewriteUI()` resets active flag; clear dedupe texts on mode switch.

```cpp
void MainWindow::hideRewriteUI(){
  if (rewritePanel) rewritePanel->hide();
  setIsRewriteFlowActive(false);
}
```

---

## 3) Sent empty text to model
**Root cause:** `hideRewriteUI()` cleared `pendingTextForRewrite`; button clicked after hide → sent empty string.  
**Fix:** Do **not** clear on hide; copy to local before hide; clear only after send.

```cpp
const QString text = pendingTextForRewrite.trimmed();
hideRewriteUI();
aiClient->sendRewriteRequest(text);
pendingTextForRewrite.clear();
```

---

## 4) Couldn’t read text reliably (browsers, modern apps)
**Root cause:** Only searched for `Edit` control; mis-assigned pattern availability; didn’t try TextPattern after empty ValuePattern; aborted on rect failure.  
**Fix:**
- Prefer `GetFocusedElement()`; fall back to `ElementFromHandle`.
- Broaden child search to **Edit / Document / Text**.
- Correctly detect `hasText` with both `IsTextPatternAvailable` and `IsTextEditPatternAvailable`.
- If ValuePattern yields empty, try TextPattern; **never** abort text read just because rect failed.

---

## 5) Panel “drifting to top-left” after clicking
**Root cause:** Anchor timer used our own panel/button as the focused element and tried to re-anchor to itself.  
**Fix:** Ignore any focused element from our own process (by `ProcessId` / `NativeWindowHandle`).

---

## 6) COM lifetime & leaks
**Fixes:**
- Replace `editBlock` by **AddRef new → Release old**; set pointers to `nullptr` after `Release()`.
- In `findChildEditControl()`, release all created conditions/arrays; remove stray `Release()` on uninitialized pointer.

---

## 7) UX polish
- Panel auto-hide only in copy mode; steady follow in anchor mode.
- Typing updates cached text while visible; no repeated popups.

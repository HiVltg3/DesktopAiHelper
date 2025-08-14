# TECHNICAL OVERVIEW

> This document consolidates the full technical story: **architecture**, **hook→UIA capture pipeline**, **AI client design**, **JSON schema**, **API examples**, **stability hardening**, and **UX details**. Use it with the top-level README.

---

## 1) Goals & Non-Goals
**Goals**
- Bring AI rewrite/translation **into any Windows app** without per-app code.
- Keep input responsive: hook thread does **no heavy work**.
- Be robust across classic and modern controls (Edit / Document / Text; Chromium editors).
- Provide **reliable write-back** with clear fallbacks.

**Non-Goals**
- Per-app hacks for special editors (e.g., hard-coded QQ workarounds). We prefer generic behavior; last-resort `SendInput` is possible but not default.

---

## 2) High-Level Architecture & Threading

```
[Keyboard Hook Thread]
   │ (WM_KEYUP)
   │ ├─ if (g_ignoreHook) → pass
   │ └─ if (!g_captureInFlight.exchange(true)) → invokeMethod(..., Qt::QueuedConnection)
   ▼
[Qt GUI Thread / Main]
   ├─ RAII guard: on exit → g_captureInFlight=false
   ├─ Target selection:
   │    • GetFocusedElement() first (accurate for browsers)
   │    • else ElementFromHandle(hwnd)
   │    • else FindAll(Edit|Document|Text) in subtree
   ├─ Read:
   │    • ValuePattern::get_CurrentValue(); if empty/unsupported →
   │    • TextPattern::get_DocumentRange()->GetText(-1)
   ├─ UI:
   │    • processCapturedText → debounce → showRewriteUI(...)
   │    • when panel visible: update cached text only (no re-popup)
   └─ Write-back:
        • ValuePattern::SetValue()
        • else Ctrl+A / Ctrl+V (g_ignoreHook=true during paste)
```

**Why**  
- Hook must be ultra-light; COM/UI work in the hook freezes input.  
- `GetFocusedElement()` yields the **actual focused node** in Chromium apps; broadened control types (Edit/Document/Text) improve hit-rate.  
- Try both text patterns (Value → Text) because many apps only expose one.  
- Some controls reject programmatic set; paste fallback ensures consistent UX.

---

## 3) Capture → Rewrite/Translate → Write-back Pipeline

### 3.1 Enqueue from the hook
```cpp
if (!g_ignoreHook.load(std::memory_order_relaxed)) {
    bool inFlight = g_captureInFlight.exchange(true, std::memory_order_acq_rel);
    if (!inFlight) {
        HWND h = hWndFocus;
        QMetaObject::invokeMethod(g_pMainWindow, [h](){
            struct Guard { ~Guard(){ g_captureInFlight.store(false, std::memory_order_release);} } _g;
            g_pMainWindow->getControlTextUIAInternal(h);
        }, Qt::QueuedConnection);
    }
}
```

### 3.2 Choose the target and read text
- Prefer **focused element**; fallback to **ElementFromHandle**; else **FindAll(Edit|Document|Text)**.  
- Read order: **ValuePattern** → **TextPattern**.  
- If `get_CurrentBoundingRectangle` fails, **do not abort**; pass invalid rect so UI can fall back to caret/mouse.

### 3.3 Debounce + show
```cpp
void MainWindow::processCapturedText(const QString& text, const QRect& rect) {
    if (text.isEmpty() || text == lastCapturedText) return;
    lastCapturedText = text;
    lastCapturedRect = rect;
    pendingTextForRewrite = text;
    if (getIsRewriteFlowActive()) return; // update only, no re-popup
    typingTimer->start(800);
}
```

### 3.4 Write-back
```cpp
bool MainWindow::setControlTextUIA(IUIAutomationElement* el, const QString& t) {
    CComPtr<IUIAutomationValuePattern> vp;
    if (el && SUCCEEDED(el->GetCurrentPatternAs(UIA_ValuePatternId, IID_IUIAutomationValuePattern, (void**)&vp)) && vp) {
        CComBSTR s(t.toStdWString().c_str());
        if (SUCCEEDED(vp->SetValue(s))) return true;
    }
    pasteTextIntoActiveControl(t); // fallback
    return true;
}

void MainWindow::pasteTextIntoActiveControl(const QString& t) {
    g_ignoreHook.store(true, std::memory_order_release);
    // set clipboard, simulate Ctrl+A / Ctrl+V ...
    QTimer::singleShot(150, this, [](){ g_ignoreHook.store(false, std::memory_order_release); });
}
```

---

## 4) AI Class

The application follows a clear separation of responsibilities and utilizes the following key components:

### 4.1 `AIClient` (Abstract Base Class)
Defines the common interface for interacting with AI services, including methods for setting API keys, sending messages, requesting conversation titles, sending rewrite requests, and requesting image generation. It also defines signals for receiving AI responses, errors, and generated titles/rewrite content/image data.

### 4.2 `GeminiClient` (Concrete Implementation)
Implements interaction with the Google Gemini API. Responsibilities:
- Store API key, construct JSON requests (including conversation history), and manage **request types** (chat, title generation, rewriting, image generation).
- Parse different response schemas and emit signals to `MainWindow`.

#### Extensibility: **Unified Network Response Handling**
To handle different AI requests efficiently, the client adopts:

- **Enum (`enum class RequestType`)** — identify the type for each request (ChatMessage, TitleGeneration, Rewrite, ImageGeneration).
```cpp
enum class RequestType {
    ChatMessage,      // Chat message request
    TitleGeneration,  // Title generation request
    Rewrite,          // Rewrite request
    ImageGeneration   // Image generation request
};
```

- **`QMap<QNetworkReply*, RequestType> requestTypeMap`** — map each async `QNetworkReply*` to its `RequestType`.

- **Unified slot (`onNetworkReplyFinished`)** — all replies land here:
  1) Lookup `RequestType` from `requestTypeMap`.  
  2) Parse the response according to its type (text vs image schemas).  
  3) Emit signals (`aiResponseReceived()`, `titleGenerated()`, `rewritedContentReceived()`, `imageGenerated()`).  
  4) Remove from map and delete the reply.

This pattern makes it easy to add new request types: extend the enum and the `switch` in the unified slot — no need to create separate per-type handlers.

### 4.3 `GPTClient` (Concrete Implementation)
Similar to `GeminiClient`, but for the OpenAI GPT API. It plugs into the same `AIClient` interface and unified response-handling flow.

### 4.4 `MainWindow` (UI + App Logic)
- Initialize chat UI, model switch, persistent rewrite panel, timers.  
- Load/save chat history to JSON.  
- Process AI responses and drive the conversation UI.  
- Coordinate rewrite modes and clipboard monitoring.  
- Implement UIA capture and write-back with fallback paste.

---

## 5) Local JSON Structure (`chathistory.json`)

```json
{
  "conversations": [
    {
      "title": "Conversation Title 1",
      "dialogue": [
        { "user": "User Message 1", "ai": "AI Response 1" },
        { "user": "User Message 2", "ai": "AI Response 2" }
      ]
    },
    {
      "title": "Conversation Title 2",
      "dialogue": [
        { "user": "User Message A", "ai": "AI Response A" }
      ]
    }
  ]
}
```

- **conversations**: array of conversation objects.  
- Each conversation has a **title** and a **dialogue** array.  
- Each dialogue entry has **user** and **ai** strings (AI image replies can be stored as data-URLs if supported by UI).

---

## 6) API Requests & Response Structures

### 6.1 Text Message Request (`sendMessage`)
Include conversation history for context:
```json
{
  "contents": [
    { "role": "user",  "parts": [ { "text": "User Historical Message 1" } ] },
    { "role": "model", "parts": [ { "text": "AI Historical Response 1" } ] },
    { "role": "user",  "parts": [ { "text": "Current User Input" } ] }
  ],
  "generationConfig": { "temperature": 0.3 }
}
```

### 6.2 Image Generation Request (`requestImageGeneration`)
```json
{
  "instances": [ { "prompt": "Image description, e.g., a cat chasing a butterfly on the grass" } ],
  "parameters": { "sampleCount": 1 }
}
```

### 6.3 API Response Formats

**Text response:**
```json
{
  "candidates": [
    {
      "content": { "role": "model", "parts": [ { "text": "This is the AI-generated response text" } ] },
      "finishReason": "STOP",
      "index": 0
    }
  ]
}
```

**Image response:**
```json
{
  "predictions": [ { "bytesBase64Encoded": "Base64 encoded image data string" } ]
}
```

---

## 7) Concurrency, Stability & COM Lifetime

- **Two atomic gates**: `g_captureInFlight` (flood control) and `g_ignoreHook` (paste window).  
- **RAII guard**: ensures `g_captureInFlight=false` on all exits.  
- **Release order**: when switching `editBlock`, **AddRef new → Release old**, then null the pointers.  
- **No “only shows once”**: `hideRewriteUI()` resets the active flag, but doesn’t clear `pendingTextForRewrite`.  
- **No “send empty”**: button handlers copy text to a local variable **before** hiding, then send, then clear.

---

## 8) Panel UX
- Persistent panel; **AnchorToRect** and **AtCursor** placements.  
- Follow timer (~200ms) repositions using the **focused element’s rect**.  
- **Ignore own process/windows** when following (via `UIA_ProcessIdPropertyId` / `UIA_NativeWindowHandlePropertyId`) to prevent drift.  
- Copy-mode auto-hide; anchor-mode stays and follows.

---

## 9) Translation (New)
The **Translate** button reuses the rewrite pipeline; default target is **English** and **overwrites** the source. To enable target selection, uncomment the prompt code or add a ComboBox in the panel.

---

## 10) Supported Apps & Limitations
- Works with legacy editors (Notepad), many IM/office apps, and Chromium-based web inputs/contenteditable.  
- Some IM editors (e.g., certain QQ inputs) **block programmatic set**; paste fallback usually works.  
- If paste is also blocked, a per-app `SendInput` path can be implemented (deliberately not enabled globally).

---

## 11) Troubleshooting
- **Panel doesn’t follow after window switch** → ensure follow timer is active in anchor mode; ignore self windows.  
- **Sends empty text** → button copies text before hide; `hideRewriteUI()` must not clear pending.  
- **Only shows once** → `isRewriteFlowActive=false` on hide; clear dedupe texts when changing modes.  
- **Can’t read in browsers** → use `GetFocusedElement()`; broaden child search; try TextPattern after Value.  
- **Drifts to top-left** → filter by process/window handles in follow loop.

---

## 12) Privacy
No keystrokes stored. Text is accessed only during explicit actions. API keys are local in `config.ini`.

---

## 13) Roadmap
- Settings (toggles, whitelists, hotkeys, auto-hide)  
- TSF (Windows) / AXAPI (macOS) adapters  
- Optional local models  
- Usability study & crash telemetry

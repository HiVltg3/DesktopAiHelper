# AI Desktop Rewrite Assistant (Windows · Qt + UIA)

A desktop AI assistant that brings **rewriting & translation** into **any Windows app**. It shows a small panel near where you type or copy, captures text via **UI Automation (UIA)**, calls an AI model (OpenAI GPT / Google Gemini), and writes back using **ValuePattern** or **fallback paste** — while the keyboard hook **only queues work** and all UI/COM run on the Qt GUI thread for stability.

> - `img/main.png` — Main UI
> - `img/anchor-mode.gif` — Typing → anchored panel (input corner)
> - `img/at-cursor.gif` — Copy → panel at cursor (auto-hide)
> - `img/translate.gif` — Translate & overwrite

---

## Features
- **Rewrite / Rewrite with Prompt** inside any app
- **Translate (New)** — one-click **to English**, overwrites source (optional target language prompt or ComboBox supported in code)
- **Two placements**: **AnchorToRect** (input corner) / **AtCursor** (copy workflows)
- **Reliable write-back**: UIA `ValuePattern::SetValue` → fallback **Ctrl+A / Ctrl+V`
- **Focused targeting**: `GetFocusedElement()` first; child search covers **Edit / Document / Text**
- **Stable concurrency**: two atomic gates (in-flight & ignore-hook) + RAII guard; hook thread does **no heavy work**
- **Model switching**: OpenAI GPT / Google Gemini; chat history in JSON
- **Engineering docs**: full architecture & API details in `docs/TECHNICAL_OVERVIEW.md`

---

## Quick Start
**Requirements**: Windows 10/11, Qt 6.x (or 5.15+) MSVC x64, CMake or Qt Creator

```bash
git clone https://github.com/<yourname>/<repo>.git
cd <repo>
cmake -S . -B build -DCMAKE_PREFIX_PATH="<Qt/6.x/msvc64>"
cmake --build build --config Release
```

Create `config.ini` next to the executable:
```ini
[API]
ChatGPTKey=YOUR_CHATGPT_API_KEY
GeminiKey=YOUR_GEMINI_API_KEY
```

Run the app, place screenshots under `img/`, and replace the placeholders in this README.

---

## Usage
- **Rewrite**: type or select text → panel appears → click **Rewrite** (or **Rewrite with prompt**).  
- **Translate**: click **Translate** to convert to **English** and **overwrite** the source.  
- If the control blocks programmatic write, the app falls back to **Ctrl+A / Ctrl+V**.

---

## Learn More
For **full technical details** (architecture, hook→UIA pipeline, AI class design, JSON schema, API examples, stability strategies), see:  
**`docs/TECHNICAL_OVERVIEW.md`**

---

## Known Limitations
- Some IM/rich editors (e.g., certain QQ inputs) may block direct write-back; paste fallback usually works.  
- Browsers may block programmatic set in web content; paste fallback applies.  
- Windows-only (UIA). Cross-platform requires TSF/AXAPI work.

---

## Privacy
- No keystrokes are stored. Text is accessed only when the panel is shown or you click actions.  
- API keys are loaded locally from `config.ini`.  
- You can disable clipboard monitoring and/or UIA capture.

---

## Roadmap
- Settings panel (toggles, whitelist, hotkeys, auto-hide)
- TSF/AXAPI adapters; optional local models
- Usability study & crash telemetry

---

## License
Add your license (e.g., MIT). If used for school applications, consider keeping the full repo private and sharing access on request.

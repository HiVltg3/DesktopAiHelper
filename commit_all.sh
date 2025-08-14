#!/bin/bash
# 一键提交四个逻辑分组（含自动处理资源文件变化）

# Commit 1: 主程序和 UI 改动
git add main.cpp mainwindow.cpp mainwindow.h mainwindow.ui
git commit -m "Enhance UIA text capture and rewrite panel logic

- Improved target element resolution using GetFocusedElement() with fallback search
- Refactored rewrite panel positioning to support both AnchorToRect and AtCursor modes
- Integrated translation button into rewrite panel
- Added timer-based follow mechanism and self-window filtering to avoid drift
- Fixed potential re-entrancy and stale text issues when switching focus or modes"

# Commit 2: AI 类增加翻译功能
git add aiclient.h aiclient.cpp geminiclient.h geminiclient.cpp chatgptclient.h chatgptclient.cpp
git commit -m "Add translation support to AI clients

Background:
- Users requested one-click translation without manual prompt entry
- Goal: Reuse existing rewrite pipeline while allowing flexible target languages

Changes:
- AIClient: Added sendTranslateRequest() to unify translation handling
- GeminiClient & ChatGPTClient: Implemented translation request with default 'English' target
- Preserved optional prompt (commented) for selecting custom target language
- Emits rewritedContentReceived() with translated text, enabling seamless overwrite of source content"

# Commit 3: 资源更新（自动包含新增/删除图片和 CMakeLists.txt）
git add resource.qrc CMakeLists.txt
# 自动添加 icons 和 img 下的所有文件（包括新增的 gif/png/svg）
git add icons/* img/*
# 自动暂存被删除的文件
git add -u icons img
git commit -m "Update resources for translation feature and UI

- Added translation icon to resource.qrc
- Updated CMakeLists.txt to include new resource files
- Added new demo images/gifs for translation and rewrite modes
- Removed outdated demo images"

# Commit 4: 文档更新
git add README.md .github/ISSUE_TEMPLATE/bug_report.md .github/PULL_REQUEST_TEMPLATE.md docs/ARCHITECTURE.md docs/DEBUGLOG.md docs/PRIVACY.md docs/ROADMAP.md docs/TECHNICAL_OVERVIEW.md CHANGELOG.md
git commit -m "Update documentation with translation feature and latest technical changes

- README.md: Added translation to Features & Usage sections
- TECHNICAL_OVERVIEW.md: Documented AIClient translation API and integration flow
- ARCHITECTURE.md: Updated sequence diagrams to include translation path
- DEBUGLOG.md: Added sample log for translation operation
- PRIVACY.md: Clarified no keystrokes stored; translation text processed same as rewrite
- ROADMAP.md: Added multi-language translation selector to future plans
- CHANGELOG.md: Logged translation feature introduction"

echo "✅ All commits completed successfully."

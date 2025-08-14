## What does this PR change?
- [ ] Prevent re-entrancy (ignoreHook + in-flight gate)
- [ ] Persistent panel (AnchorToRect / AtCursor) with auto-hide
- [ ] Focused-element targeting; Edit/Document/Text support
- [ ] UIA lifetime (AddRef/Release order & nullptr on release)
- [ ] Other (specify)

## How to verify
1) Repeated rewrite without stalls/crash
2) Copy mode: panel auto-hides; switch back to input mode pops correctly
3) Browser & Notepad: read/write (fallback paste where needed)

## Risk & rollback
- If ValuePattern fails, fallback Ctrl+A/V paste (hook ignored during paste)

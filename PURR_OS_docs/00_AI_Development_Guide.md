# Using PURR OS Docs with AI Models

These documentation files are designed to be read by both humans **and AI models** (Claude, GPT, etc.) when adapting or extending the PURR OS codebase.

## For AI-Assisted Development

When working with an AI model to modify PURR OS:

1. **Provide context docs first**
   ```
   Read these files from PURR_OS_docs/:
   - 01_Architecture.md       (overall system design)
   - 02_KITT_Kernel_Spec.md   (kernel interfaces & lifecycle)
   - 05_Boot_Sequence.md      (startup & recovery flow)
   - [target-specific docs]   (e.g., 12_TDeck_BlackBerry6_UI_Spec.md)
   ```

2. **Ask specific questions**
   - "I want to add a new sensor module. What does it need to register with KITT?"
   - "How should display drivers integrate with the boot sequence?"
   - "What's the memory layout for app bundles?"

3. **Share relevant docs**
   - Before asking to modify boot code → share `05_Boot_Sequence.md`
   - Before adding UI → share `06_WindowsCE_UI_Spec.md` or `12_TDeck_BlackBerry6_UI_Spec.md`
   - Before changing app packaging → share `04_AppBundle_Format.md`

## Documentation Structure

| Doc | Purpose |
|-----|---------|
| **01_Architecture.md** | System overview, component relationships, design philosophy |
| **02_KITT_Kernel_Spec.md** | Kernel API, task management, memory model, module lifecycle |
| **03_ControlPanel_Spec.md** | System settings, configuration storage, user preferences |
| **04_AppBundle_Format.md** | App packaging, metadata, sandboxing, resource embedding |
| **05_Boot_Sequence.md** | Hardware init, recovery UI, OTA chainload, SOS mode |
| **06_WindowsCE_UI_Spec.md** | UI shell architecture, widget system, event model |
| **09_CattoBoardV1_Spec.md** | Custom hardware reference design |
| **10_Handshake_Protocols.md** | USB, network, radio communication protocols |
| **11_PURR_HID_Edition.md** | HID device mode, input handling |
| **12_TDeck_BlackBerry6_UI_Spec.md** | T-Deck specific UI (trackball, BB6-style shell) |

## Version Notes

**PURR OS v0.8.0 / KITT v0.4.1 (Current)**

- All docs are version-controlled (tracked in git)
- Updated for ESP-IDF 5.3.5 pure migration (no Arduino IDE)
- Cross-platform builds (Windows, Linux, macOS)
- MiniWin window manager integration
- Display type targets (cyd_s028r, cyd_s024c)

### What Changed from v0.7.0 → v0.8.0

- ✅ Removed Arduino IDE dependency → pure ESP-IDF
- ✅ Added Linux/macOS support (zero codebase changes)
- ✅ MiniWin window manager as core UI layer
- ✅ Swappable UI skins (BlackBerry, Explorer, ClassicMac)
- ✅ XPT2046 & CST816S drivers unified
- ✅ Memory optimizations (target 260KB+ heap for apps)

### What Changed from KITT v0.4.0 → v0.4.1

- ✅ Display driver abstraction layer
- ✅ Touch controller registration API
- ✅ Partition manager for OTA & firmware backup
- ✅ Recovery SOS mode implementation

## Best Practices

### ✅ DO:
- Reference docs by name when asking AI to modify code
- Keep implementation details in `.h/.cpp` files; design in docs
- Update docs when adding new kernel APIs or modules
- Use docs as specification before coding

### ❌ DON'T:
- Ask AI to implement features without reading the architecture first
- Modify kernel lifecycle without checking `02_KITT_Kernel_Spec.md`
- Add new UI shells without understanding `06_WindowsCE_UI_Spec.md`
- Change boot flow without reviewing `05_Boot_Sequence.md`

## Examples

### "How do I add a WiFi-connected sensor?"

> "I want to add a BME680 environmental sensor that reports WiFi data. Where does this live in KITT?"

**Answer in docs**: 
- `02_KITT_Kernel_Spec.md` → module registration, lifecycle
- `01_Architecture.md` → sensor module tier
- System design says sensors are optional modules that register with KITT on init

### "How do I add a new UI shell?"

> "I want to add a LVGL-based dashboard shell for the CYD. What's the integration point?"

**Answer in docs**:
- `06_WindowsCE_UI_Spec.md` → widget system, event model
- `12_TDeck_BlackBerry6_UI_Spec.md` → example BlackBerry shell
- Code reference: `system/kernel/modules/blackberry_ui.cpp`

### "How do I package a MicroPython app?"

> "I want to distribute a MicroPython app that uses the touch sensor. What format?"

**Answer in docs**:
- `04_AppBundle_Format.md` → `.meow` structure, metadata, embedding
- `10_Handshake_Protocols.md` → how app communicates with KITT

## Contributing

When contributing to PURR OS:

1. **Update relevant docs first** (before or alongside code changes)
2. **Document new APIs** in the appropriate spec file
3. **Reference docs in commit messages** if major architecture changes
4. **Keep version numbers in sync** (update headers when releasing)

## Questions?

If docs are unclear or missing coverage:
- File an issue on GitHub
- Ask in AI-assisted development sessions (docs will be more accurate)
- Submit docs improvements with your code changes

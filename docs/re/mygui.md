# MyGUI in Kenshi

- `MyGUIEngine_x64.dll` (MSVC 2010), 3598 exports. Kenshi's menus are MyGUI layouts in
  `data/gui/layout/` (main menu: `Kenshi_MainMenu.layout`, buttons `ContinueButton`, `NewGameButton`,
  `LoadGameButton`, `ImportGameButton`, `OptionsButton`, `CreditsButton`, `ExitButton`, skin `Kenshi_Button1`).
- Skins used by Kenshi: windows `Kenshi_WindowCX`, edit boxes `Kenshi_EditBox`, text
  `Kenshi_TextboxStandardText`, buttons `Kenshi_Button1` / `Kenshi_Button2`. Layers include `Popup`, `Window`, `Main`.

## VC10 `std::string` layout (x64)

Observed in `Gui::findWidgetT`, which builds a local string: buffer at +0x00 (16 bytes, small-string
optimisation), size at +0x10, capacity at +0x18 initialised to 15. **32 bytes, no allocator field.**
We only pass strings as `const std::string&` (MyGUI copies them), so our own allocation is safe.

## Functions we call (resolved by exported name)

`Singleton<Gui>::getInstancePtr`, `Singleton<InputManager>::getInstancePtr`, `Gui::findWidgetT(const std::string&, bool)`,
`Widget::getParent`, `Widget::createWidgetRealT(type, skin, FloatCoord, Align, name)`,
`Gui::createWidgetRealT(type, skin, FloatCoord, Align, layer, name)`, `Widget::setProperty(key, value)`
(`"Caption"`), `Gui::destroyWidget`, `InputManager::getMouseFocusWidget`, `EditBox::getCaption` + `UString::asUTF8_c_str`.

`Align::Default` = Left (2) | Top (8) = 10. Clicks are detected by polling the mouse-focus widget
(no MyGUI delegates are created from our side).

## Layout name prefix (found in game 2026-11-12)

Kenshi loads its layouts with a per-instance prefix: widget names look like
`0,000,000,07F,D80,4B0_NewGameButton` (an address formatted with thousands separators), different on
every run. Exact-name lookups (`findWidgetT("NewGameButton")`) therefore fail. We search the root
widgets (`Gui + 0x28` .. `Gui + 0x30`, a `std::vector<Widget*>` iterated by `Gui::findWidgetT`)
and their children (`Widget::getChildCount` / `getChildAt`) for a name **ending** with `_NewGameButton`.
Widgets we create ourselves have unprefixed names.

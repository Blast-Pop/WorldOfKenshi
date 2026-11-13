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

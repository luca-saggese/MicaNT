# MacShell reference and attribution

The desktop Clock geometry, update behavior, and Liquid Glass rendering formulas
in this feature were derived from the MIT-licensed project
[0-ss/MacShell](https://github.com/0-ss/MacShell), especially `MacWidgets.qml`,
`ClockWidget.qml`, `WidgetVisual.qml`, `CardWidget.qml`, and `liquidglass.frag`.

Copyright (c) 2026 Rickky. The full MIT license is in `LICENSE`.

LuceOS does not include MacShell's QML, Qt/Quickshell dependencies, fonts, or
Apple artwork. The C++23 software renderer in `tools/liquid_glass_effect.cpp`
is an adaptation of the shader behavior; it uses bounded CPU buffers and
approximates the original blur/refraction sampling. Explorer uses system GDI
text because the original widget font is not included. Pixel identity has not
been established.

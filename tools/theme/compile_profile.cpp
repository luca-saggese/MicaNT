#include "micant/um/subsystems/theme/native_theme.hpp"

#include <fstream>
#include <iterator>
#include <string>

int main(int argc, char** argv) {
    using namespace micant::um::theme;
    if (argc != 3 && argc != 4) return 2;
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) return 3;
    std::string css((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    CompiledCss parsed{};
    CssParseError error{};
    if (compileGtkCss(css, parsed, &error) != CssStatus::Success) return 4;
    NativeTheme theme{};
    CssAccentOverride accent{};
    if (argc == 4) {
        if (!parseHexColor(argv[3], accent.replacement)) return 5;
        accent.enabled = true;
    }
    if (!buildNativeTheme(parsed, theme, {36, 36, 36, 255}, accent)) return 6;
    const MicaThemeSnapshot snapshot = makeThemeSnapshot(theme);
    if (!MicaThemeSnapshotIsValid(&snapshot, sizeof(snapshot))) return 7;
    std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
    if (!output) return 8;
    output.write(reinterpret_cast<const char*>(&snapshot), sizeof(snapshot));
    return output ? 0 : 9;
}

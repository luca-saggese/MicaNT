/**
 * @file mac_shell_app.cpp
 * @brief Mac-style desktop widgets, Control Center and Dock for MicaNT.
 *
 * The desktop remains owned by Explorer. These owned popup windows are a
 * separate Ring 3 process and publish their glass descriptors to the existing
 * compositor service.
 */
#define WIN32_LEAN_AND_MEAN
#include "explorer_shell/icon_catalog.hpp"
#include "gui_common.hpp"
#include "micant/shared/native_nt_api.hpp"
#include "micant/shared/native_process_abi.hpp"
#include "micant/shared/system_time.hpp"
#include "micant/shared/user_syscall.hpp"
#include "micant/shared/win32k_client.h"
#include "micant/um/subsystems/imaging/bmp_png_decoder.hpp"
#include "micant/um/subsystems/imaging/native_image_allocator.hpp"
#include "micant/um/subsystems/imaging/native_image_file.hpp"
#include <cstring>
#include <windows.h>

extern "C" __declspec(dllimport) int32_t
LuceGlassSetWindow(void *, const MicaLiquidGlassDescriptor *) noexcept;

namespace {
using namespace micant_gui;
constexpr uint32_t CARD = 168, CLOCK_WIN = 188, CC_W = 313, CC_H = 510;
constexpr uint32_t DOCK_W = 536, DOCK_H = 105;
constexpr UINT WM_MACSHELL_CLOCK_TICK = 0x8004u;

struct Surface {
  HWND hwnd{};
  HDC dc{};
  HBITMAP bitmap{};
  uint32_t *px{};
  int32_t x{}, y{};
  uint32_t w{}, h{}, top{}, glassH{};
};
struct IconCache {
  micant::explorer::IconId id{};
  uint8_t *pixels{};
  size_t bytes{};
  uint32_t w{}, h{}, stride{};
  micant::um::imaging::ImageAllocator allocator{};
};
struct State {
  HWND owner{};
  uint32_t sw{1024}, sh{768};
  Surface clock{}, calendar{}, center{}, dock{};
  IconCache icons[9]{};
  int32_t pointerX{-1};
  uint8_t second{0xff};
  uint16_t year{2026};
  uint8_t month{1}, day{1}, hour{}, minute{}, sec{};
  HFONT small{}, medium{}, clockFont{};
};
State g{};
constexpr wchar_t kClass[] = L"MicaNT.MacShell.Widget";

void fill(uint32_t *p, uint32_t w, uint32_t h, int x, int y, int rw, int rh,
          uint32_t c) noexcept {
  if (!p || rw <= 0 || rh <= 0)
    return;
  int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
  int x1 = x + rw > int(w) ? int(w) : x + rw,
      y1 = y + rh > int(h) ? int(h) : y + rh;
  for (int yy = y0; yy < y1; ++yy)
    for (int xx = x0; xx < x1; ++xx)
      p[size_t(yy) * w + xx] = c;
}
uint32_t glassColor(uint8_t r, uint8_t green, uint8_t b, uint8_t a) noexcept {
  auto scale = [a](uint8_t c) {
    return uint8_t((uint32_t(c) * a + 127u) / 255u);
  };
  return PackBGRA(scale(r), scale(green), scale(b), a);
}
void rounded(uint32_t *p, uint32_t w, uint32_t h, int x, int y, int rw, int rh,
             int r, uint32_t c) noexcept {
  if (rw <= 0 || rh <= 0)
    return;
  if (r > rw / 2)
    r = rw / 2;
  if (r > rh / 2)
    r = rh / 2;
  for (int yy = y; yy < y + rh; ++yy)
    for (int xx = x; xx < x + rw; ++xx) {
      int cx = xx < x + r ? x + r : xx >= x + rw - r ? x + rw - r - 1 : xx;
      int cy = yy < y + r ? y + r : yy >= y + rh - r ? y + rh - r - 1 : yy;
      int dx = xx - cx, dy = yy - cy;
      if (dx * dx + dy * dy <= r * r && xx >= 0 && yy >= 0 && xx < int(w) &&
          yy < int(h))
        p[size_t(yy) * w + xx] = c;
    }
}
void label(uint32_t *p, uint32_t w, uint32_t h, int x, int y, const char *s,
           uint32_t c) noexcept {
  if (!p || !s || x < 0 || y < 0)
    return;
  HDC dc = nullptr;
  HFONT font = g.small;
  if (p == g.clock.px) {
    dc = g.clock.dc;
    font = y > 60 ? g.clockFont : g.small;
  } else if (p == g.calendar.px) {
    dc = g.calendar.dc;
    font = y < 34 ? g.medium : g.small;
  } else if (p == g.center.px)
    dc = g.center.dc;
  if (!dc || !font) {
    DrawStringClipped(p, w, h, x, y, s, c);
    return;
  }
  wchar_t wide[96]{};
  size_t n = 0;
  while (s[n] && n + 1 < std::size(wide)) {
    wide[n] = static_cast<unsigned char>(s[n]);
    ++n;
  }
  if (!SelectObject(dc, font)) {
    DrawStringClipped(p, w, h, x, y, s, c);
    return;
  }
  (void)SetTextColor(dc, RGB((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff));
  (void)SetBkMode(dc, TRANSPARENT);
  if (!TextOutW(dc, x, y, wide, static_cast<int>(n)))
    DrawStringClipped(p, w, h, x, y, s, c);
}
bool makeSurface(Surface &s, int x, int y, uint32_t w, uint32_t h, uint32_t top,
                 uint32_t gh, float radius, float blur,
                 bool widget = false) noexcept {
  s = {};
  s.x = x;
  s.y = y;
  s.w = w;
  s.h = h;
  s.top = top;
  s.glassH = gh;
  const DWORD exStyle =
      WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | (widget ? 0 : WS_EX_TOPMOST);
  s.hwnd = CreateWindowExW(exStyle, kClass, L"", WS_POPUP | WS_VISIBLE, x, y, w,
                           h, g.owner, nullptr, nullptr, nullptr);
  if (!s.hwnd)
    return false;
  s.dc = CreateCompatibleDC(nullptr);
  if (!s.dc)
    return false;
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -LONG(h);
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void *bits{};
  s.bitmap = CreateDIBSection(s.dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!s.bitmap || !bits)
    return false;
  SelectObject(s.dc, s.bitmap);
  s.px = static_cast<uint32_t *>(bits);
  MicaLiquidGlassDescriptor d{};
  d.magic = MICANT_LIQUID_GLASS_MAGIC;
  d.version = MICANT_LIQUID_GLASS_VERSION;
  d.size = sizeof(d);
  d.flags = MICANT_LIQUID_GLASS_ACTIVE;
  d.top = top;
  d.width = w;
  d.height = gh;
  d.radius = radius;
  d.bezel = widget ? 26.0f : 18.0f;
  d.thickness = widget ? 30.0f : 20.0f;
  d.dispersion = .06f;
  d.blurPx = 1.0f;
  d.tintMix = widget ? .586f : .25f;
  d.saturation = 1.25f;
  d.dark = 1.0f;
  d.sourceBlur = blur;
  d.downsample = .5f;
  d.pad = 10;
  d.rim = .45f;
  d.grain = .0045f;
  d.magnify = .042857f;
  d.rimTint = 1;
  d.flatInner = 1;
  d.generation = 1;
  if (widget) {
    d.left = 10;
    d.top = 10;
    d.width = CARD;
    d.height = CARD;
  }
  if (!LuceGlassSetWindow(s.hwnd, &d))
    return false;
  return true;
}
void present(HWND hwnd) noexcept {
  if (hwnd) {
    InvalidateRect(hwnd, nullptr, FALSE);
    UpdateWindow(hwnd);
  }
}
void present(Surface &s) noexcept {
  if (s.hwnd && s.px)
    present(s.hwnd);
}
void paint(Surface &s) noexcept {
  if (!s.px)
    return;
  PAINTSTRUCT ps{};
  HDC dst = BeginPaint(s.hwnd, &ps);
  if (dst)
    BitBlt(dst, 0, 0, s.w, s.h, s.dc, 0, 0, SRCCOPY);
  EndPaint(s.hwnd, &ps);
}
void mixIcon(uint32_t *p, uint32_t w, uint32_t h, int x, int y, int size,
             const IconCache &i) noexcept {
  if (!i.pixels || !i.w || !i.h)
    return;
  const auto sample = [&](uint32_t sx, uint32_t sy) {
    return reinterpret_cast<const uint32_t *>(i.pixels +
                                              size_t(sy) * i.stride)[sx];
  };
  for (int yy = 0; yy < size; ++yy)
    for (int xx = 0; xx < size; ++xx) {
      int dx = x + xx, dy = y + yy;
      if (dx < 0 || dy < 0 || dx >= int(w) || dy >= int(h))
        continue;
      float fx = (float(xx) + .5f) * i.w / size - .5f,
            fy = (float(yy) + .5f) * i.h / size - .5f;
      if (fx < 0)
        fx = 0;
      if (fy < 0)
        fy = 0;
      if (fx > i.w - 1)
        fx = float(i.w - 1);
      if (fy > i.h - 1)
        fy = float(i.h - 1);
      uint32_t x0 = uint32_t(fx), y0 = uint32_t(fy),
               x1 = x0 + 1 < i.w ? x0 + 1 : x0, y1 = y0 + 1 < i.h ? y0 + 1 : y0;
      uint32_t c00 = sample(x0, y0), c10 = sample(x1, y0), c01 = sample(x0, y1),
               c11 = sample(x1, y1);
      float tx = fx - x0, ty = fy - y0;
      uint32_t color = 0;
      for (unsigned shift : {0u, 8u, 16u}) {
        auto premul = [shift](uint32_t c) {
          return float((c >> shift) & 255u) * float(c >> 24) / 255.0f;
        };
        float top = premul(c00) * (1 - tx) + premul(c10) * tx,
              bottom = premul(c01) * (1 - tx) + premul(c11) * tx;
        color |= uint32_t(top * (1 - ty) + bottom * ty + .5f) << shift;
      }
      float at = float(c00 >> 24) * (1 - tx) + float(c10 >> 24) * tx,
            ab = float(c01 >> 24) * (1 - tx) + float(c11 >> 24) * tx;
      uint8_t alpha = uint8_t(at * (1 - ty) + ab * ty + .5f);
      if (alpha)
        p[size_t(dy) * w + dx] = color | (uint32_t(alpha) << 24);
    }
}
void loadIcons() noexcept {
  using namespace micant::explorer;
  constexpr IconId ids[]{
      IconId::Computer,   IconId::Network,  IconId::Documents,
      IconId::Pictures,   IconId::Music,    IconId::Videos,
      IconId::Executable, IconId::HardDisk, IconId::TrashEmpty};
  const auto *shared = reinterpret_cast<const micant::KUserSharedData *>(
      micant::UserSharedDataAddress);
  size_t root = 0;
  while (root < std::size(shared->ntSystemRoot) && shared->ntSystemRoot[root])
    ++root;
  if (root < 3)
    return;
  for (size_t n = 0; n < 9; ++n) {
    auto &out = g.icons[n];
    out.id = ids[n];
    const IconAsset *a = FindIconAsset(ids[n]);
    if (!a)
      continue;
    wchar_t path[320]{};
    size_t c = 0;
    for (; c < root; ++c)
      path[c] = wchar_t(shared->ntSystemRoot[c]);
    constexpr char16_t pre[] = u"\\System\\Icons\\WhiteSur\\";
    for (size_t j = 0; j < std::size(pre) - 1; ++j)
      path[c++] = wchar_t(pre[j]);
    for (size_t j = 0; a->category[j]; ++j)
      path[c++] = wchar_t(a->category[j]);
    path[c++] = L'\\';
    path[c++] = L'4';
    path[c++] = L'8';
    path[c++] = L'\\';
    for (size_t j = 0; a->fileName[j]; ++j)
      path[c++] = wchar_t(a->fileName[j]);
    path[c] = 0;
    auto alloc = micant::um::imaging::NativeImageAllocator();
    micant::um::imaging::ImageLimits lim{};
    lim.maxEncodedBytes = 2 * 1024 * 1024;
    lim.maxPixels = 65536;
    lim.maxDecodedBytes = 262144;
    lim.maxIntermediateBytes = 2 * 1024 * 1024;
    micant::um::imaging::ImageFrame frame{};
    if (micant::um::imaging::DecodeImageFile(path, frame, alloc, lim) ==
        micant::um::imaging::ImageStatus::Success) {
      out.pixels = frame.pixels.releaseOwnership(out.bytes, out.allocator);
      out.w = frame.width;
      out.h = frame.height;
      out.stride = frame.stride;
    }
  }
}
void drawDock() noexcept {
  auto &s = g.dock;
  if (!s.px)
    return;
  std::memset(s.px, 0, size_t(s.w) * s.h * 4);
  constexpr int icon = 52, cell = 58, first = 10, top = 8 + icon;
  for (int i = 0; i < 9; ++i) {
    int cx = first + i * cell + icon / 2;
    int globalCx = s.x + cx;
    int dist = g.pointerX < 0
                   ? 999
                   : ((g.pointerX > globalCx ? g.pointerX - globalCx
                                             : globalCx - g.pointerX));
    float t = float(dist) / 116.0f;
    float inf = t >= 1 ? 0 : 1 - t * t * (3 - 2 * t);
    int sz = int(icon * (1 + .70f * inf) + .5f);
    mixIcon(s.px, s.w, s.h, cx - sz / 2, top - sz, sz, g.icons[i]);
    if (i < 8)
      fill(s.px, s.w, s.h, cx - 2, int(s.top + s.glassH) - 8, 4, 3,
           glassColor(235, 237, 244, 180));
  }
  present(s);
}
void drawCenter() noexcept {
  auto &s = g.center;
  if (!s.px)
    return;
  auto *p = s.px;
  uint32_t w = s.w, h = s.h;
  std::memset(p, 0, size_t(w) * h * 4);
  uint32_t white = PackBGRA(248, 249, 253), sub = PackBGRA(190, 197, 212);
  label(p, w, h, 18, 16, "Control Center", white);
  rounded(p, w, h, 12, 48, 140, 164, 21, glassColor(24, 37, 62, 180));
  rounded(p, w, h, 161, 48, 140, 164, 21, glassColor(24, 37, 62, 180));
  label(p, w, h, 24, 59, "Connectivity", white);
  constexpr const char *names[]{"Wi-Fi", "Bluetooth", "AirDrop"};
  constexpr const char *state[]{"Home Network", "On", "Contacts Only"};
  for (int i = 0; i < 3; ++i) {
    int y = 89 + i * 38;
    rounded(p, w, h, 22, y, 25, 25, 8, glassColor(65, 142, 247, 245));
    label(p, w, h, 30, y + 7, i == 0 ? "W" : i == 1 ? "B" : "A", white);
    label(p, w, h, 54, y + 2, names[i], white);
    label(p, w, h, 54, y + 15, state[i], sub);
  }
  label(p, w, h, 173, 59, "Quick Actions", white);
  rounded(p, w, h, 173, 87, 48, 48, 11, glassColor(91, 124, 185, 185));
  label(p, w, h, 173, 143, "Desktop", white);
  label(p, w, h, 173, 159, "Appearance", sub);
  constexpr const char *toggles[]{"Airplane", "Focus", "Record", "Night"};
  for (int i = 0; i < 4; ++i) {
    int x = 12 + i * 72;
    rounded(p, w, h, x, 224, 66, 68, 18, glassColor(27, 34, 51, 155));
    rounded(p, w, h, x + 20, 231, 26, 26, 13, glassColor(72, 126, 239, 240));
    label(p, w, h, x + 29, 240,
          i == 0   ? "A"
          : i == 1 ? "Z"
          : i == 2 ? "R"
                   : "N",
          white);
    label(p, w, h, x + 7, 268, toggles[i], white);
  }
  const auto slider = [&](int y, const char *name, int amount) {
    int x = 17, tw = 279, th = 58, thumb = x + 12 + (tw - 24) * amount / 100;
    rounded(p, w, h, x, y, tw, th, 29, glassColor(28, 34, 48, 145));
    rounded(p, w, h, x, y, thumb - x, th, 29, glassColor(239, 242, 249, 226));
    label(p, w, h, x + 14, y + 25, name,
          amount > 20 ? PackBGRA(35, 40, 51) : white);
    rounded(p, w, h, thumb - 12, y + 17, 24, 24, 12,
            glassColor(255, 255, 255, 255));
  };
  slider(312, "Display", 78);
  slider(394, "Sound", 67);
  present(s);
}
void drawCalendar() noexcept {
  auto &s = g.calendar;
  if (!s.px)
    return;
  auto *p = s.px;
  uint32_t w = s.w, h = s.h;
  std::memset(p, 0, size_t(w) * h * 4);
  constexpr const char *months[]{
      "JANUARY", "FEBRUARY", "MARCH",     "APRIL",   "MAY",      "JUNE",
      "JULY",    "AUGUST",   "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};
  constexpr const char *weekdays[]{"S", "M", "T", "W", "T", "F", "S"};
  label(p, w, h, 22, 20, months[g.month - 1], PackBGRA(255, 69, 58));
  for (int col = 0; col < 7; ++col)
    label(p, w, h, 24 + col * 18, 44, weekdays[col],
          PackBGRA(255, 255, 255, 150));
  label(p, w, h, 147, 20, "<", PackBGRA(255, 255, 255, 190));
  label(p, w, h, 164, 20, ">", PackBGRA(255, 255, 255, 190));
  const int lead =
      int((micant::shared::time::daysSince1601(g.year, g.month, 1) + 1) % 7);
  const int days = int(micant::shared::time::daysInMonth(g.year, g.month));
  for (int i = 0; i < 42; ++i) {
    int day = i - lead + 1;
    if (day < 1 || day > days)
      continue;
    int x = 19 + (i % 7) * 18, y = 66 + (i / 7) * 18;
    if (day == g.day)
      rounded(p, w, h, x - 2, y - 2, 18, 18, 9, PackBGRA(255, 69, 58));
    char d[3]{char('0' + day / 10), char('0' + day % 10), 0};
    if (day < 10)
      d[0] = d[1], d[1] = 0;
    label(p, w, h, x + 2, y + 3, d, PackBGRA(255, 255, 255));
  }
  present(s);
}
void drawClock() noexcept {
  auto &s = g.clock;
  if (!s.px)
    return;
  std::memset(s.px, 0, size_t(s.w) * s.h * 4);
  constexpr float pi = 3.14159265f;
  // Place the 60 marks around the rounded clock face; the compositor draws
  // the shared Liquid Glass material behind this transparent DIB.
  const auto line = [&](int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1,
        sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy;
    for (;;) {
      if (x0 >= 0 && y0 >= 0 && x0 < int(s.w) && y0 < int(s.h))
        s.px[size_t(y0) * s.w + x0] = color;
      if (x0 == x1 && y0 == y1)
        break;
      int e = 2 * err;
      if (e > -dy) {
        err -= dy;
        x0 += sx;
      }
      if (e < dx) {
        err += dx;
        y0 += sy;
      }
    }
  };
  const auto sincos = [&](float angle, float &sine, float &cosine) {
    float z = angle;
    while (z > pi)
      z -= 2 * pi;
    while (z < -pi)
      z += 2 * pi;
    float sign = 1;
    if (z > pi / 2) {
      z = pi - z;
      sign = -1;
    } else if (z < -pi / 2) {
      z = -pi - z;
      sign = -1;
    }
    float z2 = z * z;
    sine = z - z * z2 / 6 + z * z2 * z2 / 120 - z * z2 * z2 * z2 / 5040;
    cosine = sign * (1 - z2 / 2 + z2 * z2 / 24 - z2 * z2 * z2 / 720);
  };
  for (int i = 0; i < 60; ++i) {
    float a = float(i) * 2 * pi / 60;
    float sx = 0, sy = 0;
    sincos(a, sx, sy);
    const float radius = 70.0f;
    const float inner = radius - ((i % 5) == 0 ? 7.0f : 4.0f);
    line(94 + int(sx * radius), 94 - int(sy * radius), 94 + int(sx * inner),
         94 - int(sy * inner),
         glassColor(255, 255, 255, (i % 5) == 0 ? 225 : 165));
  }
  const auto hand = [&](float angle, float length, uint32_t color) {
    float sx = 0, sy = 0;
    sincos(angle, sx, sy);
    line(94, 94, 94 + int(sx * length), 94 - int(sy * length), color);
  };
  hand((float(g.hour % 12) + float(g.minute) / 60) * 2 * pi / 12, 38,
       PackBGRA(255, 255, 255));
  hand(float(g.minute) * 2 * pi / 60, 54, PackBGRA(255, 255, 255));
  hand(float(g.sec) * 2 * pi / 60, 59, PackBGRA(255, 69, 58));
  rounded(s.px, s.w, s.h, 91, 91, 6, 6, 3, PackBGRA(255, 255, 255));
  const uint8_t hour = g.hour % 12 ? g.hour % 12 : 12;
  char time[6]{};
  size_t timeLen = 0;
  if (hour >= 10)
    time[timeLen++] = char('0' + hour / 10);
  time[timeLen++] = char('0' + hour % 10);
  time[timeLen++] = ':';
  time[timeLen++] = char('0' + g.minute / 10);
  time[timeLen++] = char('0' + g.minute % 10);
  wchar_t wide[6]{};
  for (size_t i = 0; i < timeLen; ++i)
    wide[i] = time[i];
  SIZE extent{};
  SelectObject(s.dc, g.clockFont);
  GetTextExtentPoint32W(s.dc, wide, int(timeLen), &extent);
  const int textX = 10 + (int(CARD) - extent.cx) / 2;
  label(s.px, s.w, s.h, 76, 38, "ROME", PackBGRA(255, 255, 255, 170));
  label(s.px, s.w, s.h, textX, 91, time, PackBGRA(255, 255, 255));
  present(s);
}
void refreshTime() noexcept {
  const uint16_t oldYear = g.year;
  const uint8_t oldMonth = g.month, oldDay = g.day;
  const bool first = g.second == 0xff;
  SYSTEMTIME t{};
  GetSystemTime(&t);
  micant::shared::time::SystemTimeFields utc{
      t.wYear,   t.wMonth,  t.wDay,          t.wHour,
      t.wMinute, t.wSecond, t.wMilliseconds, 0};
  uint64_t ticks{};
  if (micant::shared::time::toSystemTime100ns(utc, ticks)) {
    const auto lastSunday = [](uint16_t year, uint16_t month) {
      uint32_t last = micant::shared::time::daysInMonth(year, month);
      uint32_t weekday = uint32_t(
          (micant::shared::time::daysSince1601(year, month, last) + 1) % 7);
      return last - weekday;
    };
    micant::shared::time::SystemTimeFields start{
        t.wYear, 3, uint16_t(lastSunday(t.wYear, 3)), 1, 0, 0},
        end{t.wYear, 10, uint16_t(lastSunday(t.wYear, 10)), 1, 0, 0};
    uint64_t a{}, b{};
    bool dst = micant::shared::time::toSystemTime100ns(start, a) &&
               micant::shared::time::toSystemTime100ns(end, b) && ticks >= a &&
               ticks < b;
    uint64_t offset =
        uint64_t(dst ? 120 : 60) * micant::shared::time::TicksPerMinute;
    micant::shared::time::SystemTimeFields local{};
    if (ticks <= UINT64_MAX - offset &&
        micant::shared::time::fromSystemTime100ns(ticks + offset, local)) {
      g.year = local.year;
      g.month = uint8_t(local.month);
      g.day = uint8_t(local.day);
      g.hour = uint8_t(local.hour);
      g.minute = uint8_t(local.minute);
      g.sec = uint8_t(local.second);
    }
  }
  const bool calendarChanged =
      first || oldYear != g.year || oldMonth != g.month || oldDay != g.day;
  if (g.second != g.sec || first) {
    g.second = g.sec;
    drawClock();
  }
  if (calendarChanged)
    drawCalendar();
}
LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == 0x0081)
    return 1;
  if (msg == WM_PAINT) {
    if (hwnd == g.clock.hwnd)
      paint(g.clock);
    else if (hwnd == g.calendar.hwnd)
      paint(g.calendar);
    else if (hwnd == g.center.hwnd)
      paint(g.center);
    else if (hwnd == g.dock.hwnd)
      paint(g.dock);
    return 0;
  }
  if (msg == WM_MOUSEMOVE && hwnd == g.dock.hwnd) {
    int x = int16_t(uint32_t(lp) & 0xffff) + g.dock.x;
    if (x != g.pointerX) {
      g.pointerX = x;
      drawDock();
    }
    return 0;
  }
  if (msg == WM_LBUTTONDOWN && hwnd == g.calendar.hwnd) {
    int x = int16_t(uint32_t(lp) & 0xffff), y = int16_t(uint32_t(lp) >> 16);
    if (y < 34 && x >= 140) {
      if (x < 160) {
        if (g.month == 1) {
          g.month = 12;
          --g.year;
        } else
          --g.month;
      } else {
        if (g.month == 12) {
          g.month = 1;
          ++g.year;
        } else
          ++g.month;
      }
      drawCalendar();
    }
    return 0;
  }
  if (msg == WM_MACSHELL_CLOCK_TICK) {
    refreshTime();
    if (g.pointerX >= 0) {
      POINT pt{};
      if (GetCursorPos(&pt)) {
        if (pt.x < g.dock.x || pt.x >= g.dock.x + int(g.dock.w) ||
            pt.y < g.dock.y || pt.y >= g.dock.y + int(g.dock.h)) {
          g.pointerX = -1;
          drawDock();
        }
      }
    }
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}
bool parseOwner() noexcept {
  micant::shared::native_process::UnicodeString64 commandLine{};
  if (!micant::shared::native_process::currentCommandLine(commandLine))
    return false;
  const auto *c = reinterpret_cast<const wchar_t *>(commandLine.Buffer);
  const auto *end = c + commandLine.Length / sizeof(wchar_t);
  while (c < end && *c != L' ' && *c != L'\t')
    ++c;
  while (c < end && (*c == L' ' || *c == L'\t'))
    ++c;
  uintptr_t v = 0;
  bool any = false;
  while (c < end && *c >= L'0' && *c <= L'9') {
    any = true;
    v = v * 10 + uintptr_t(*c - L'0');
    ++c;
  }
  g.owner = reinterpret_cast<HWND>(v);
  return any && g.owner;
}

DWORD WINAPI clockWorker(void *) noexcept {
  micant::LargeInteger oneSecond{};
  oneSecond.QuadPart = -10000000;
  for (;;) {
    (void)micant::native_nt::NtDelayExecution(false, &oneSecond);
    if (!PostMessageW(g.clock.hwnd, WM_MACSHELL_CLOCK_TICK, 0, 0))
      return 1;
  }
}
} // namespace

extern "C" void _start(const char * = nullptr) {
  UserVideoInfo64 video{};
  micant_gui::DoSyscall(0x80, reinterpret_cast<uint64_t>(&video), 0);
  if (video.width)
    g.sw = video.width;
  if (video.height)
    g.sh = video.height;
  if (g.sw < 640 || g.sh < 480) {
    g.sw = 1024;
    g.sh = 768;
  }
  if (!parseOwner())
    return;
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = wndproc;
  wc.lpszClassName = kClass;
  if (!RegisterClassExW(&wc))
    return;
  g.small =
      CreateFontW(-11, 0, 0, 0, 400, FALSE, FALSE, FALSE, ANSI_CHARSET,
                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                  DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  g.medium =
      CreateFontW(-13, 0, 0, 0, 600, FALSE, FALSE, FALSE, ANSI_CHARSET,
                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                  DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  g.clockFont =
      CreateFontW(-59, 0, 0, 0, 600, FALSE, FALSE, FALSE, ANSI_CHARSET,
                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                  DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  loadIcons();
  int cx = int(g.sw) - 291, cy = 46;
  makeSurface(g.clock, cx, cy, CLOCK_WIN, CLOCK_WIN, 0, CLOCK_WIN, 29.8f, 11.2f,
              true);
  makeSurface(g.calendar, cx - 207, cy, CLOCK_WIN, CLOCK_WIN, 0, CLOCK_WIN,
              29.8f, 11.2f, true);
  makeSurface(g.center, int(g.sw) - 291 - CC_W - 16, 28, CC_W, CC_H, 0, CC_H,
              25, 8);
  int dx = (int(g.sw) - int(DOCK_W)) / 2;
  makeSurface(g.dock, dx, int(g.sh) - int(DOCK_H) - 6, DOCK_W, DOCK_H, 36, 69,
              18, 13);
  drawCenter();
  drawDock();
  refreshTime();
  micant::Handle clockThread{};
  const auto threadStatus = micant::native_nt::NtCreateThreadEx(
      &clockThread, 0x00100002u, nullptr, static_cast<micant::Handle>(-1),
      reinterpret_cast<void *>(clockWorker), nullptr, 0, 0, 0, 0, nullptr);
  if (threadStatus == micant::NtStatus::Success && clockThread)
    (void)micant::native_nt::NtClose(clockThread);
  else
    PrintLine("[MicaNT MacShell] Clock timer worker could not start");
  PrintLine("[MicaNT MacShell] Clock, Calendar, Control Center and Dock "
            "started in mac_shell.exe");
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

#pragma once

#include <GfxRenderer.h>

#include <cstdlib>

// Vector "Cipher daemon" diamond emblem — the Xteink X4 OS brand mark from the
// CipherCodex OS design comp (boot + sleep screens). Layered rotated squares:
// a halftone outer band, a hollow mid ring, and a solid core, with N/S/E/W
// ticks. Pure 1-bit vector drawing so it scales and needs no bitmap asset.
namespace CipherEmblem {

// Filled diamond (rotated square) centered at (cx,cy); rad = half-diagonal.
inline void fillDiamond(const GfxRenderer& r, int cx, int cy, int rad, bool black = true) {
  const int xs[4] = {cx, cx + rad, cx, cx - rad};
  const int ys[4] = {cy - rad, cy, cy + rad, cy};
  r.fillPolygon(xs, ys, 4, black);
}

// Diamond outline of the given stroke thickness.
inline void strokeDiamond(const GfxRenderer& r, int cx, int cy, int rad, int thickness, bool black = true) {
  if (rad <= 0) return;
  r.drawLine(cx, cy - rad, cx + rad, cy, thickness, black);
  r.drawLine(cx + rad, cy, cx, cy + rad, thickness, black);
  r.drawLine(cx, cy + rad, cx - rad, cy, thickness, black);
  r.drawLine(cx - rad, cy, cx, cy - rad, thickness, black);
}

// Full boot/sleep emblem. `rad` is the outer diamond's half-diagonal.
inline void drawEmblem(const GfxRenderer& r, int cx, int cy, int rad) {
  const int mid = rad * 56 / 100;
  const int core = rad * 22 / 100;

  // Halftone band: dotted fill inside the outer diamond and outside the mid
  // diamond, using Manhattan distance |dx|+|dy| as the rotated-square metric.
  for (int y = cy - rad; y <= cy + rad; ++y) {
    for (int x = cx - rad; x <= cx + rad; ++x) {
      const int d = std::abs(x - cx) + std::abs(y - cy);
      if (d <= rad && d > mid && ((x + y) & 1) == 0) r.drawPixel(x, y, true);
    }
  }

  strokeDiamond(r, cx, cy, rad, 3);    // outer edge
  strokeDiamond(r, cx, cy, mid, 2);    // mid ring
  fillDiamond(r, cx, cy, core, true);  // solid core

  // N/S/E/W registration ticks pointing outward from the outer points.
  const int tick = rad / 6;
  const int gap = rad < 44 ? 4 : rad / 11;
  r.drawLine(cx, cy - rad - gap, cx, cy - rad - gap - tick, true);
  r.drawLine(cx, cy + rad + gap, cx, cy + rad + gap + tick, true);
  r.drawLine(cx - rad - gap, cy, cx - rad - gap - tick, cy, true);
  r.drawLine(cx + rad + gap, cy, cx + rad + gap + tick, cy, true);
}

// Compact solid diamond token for wordmark headers (the "◆").
inline void drawGlyph(const GfxRenderer& r, int cx, int cy, int rad, bool black = true) {
  fillDiamond(r, cx, cy, rad, black);
}

}  // namespace CipherEmblem

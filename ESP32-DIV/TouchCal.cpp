#include "TouchCal.h"

#include <Preferences.h>
#include <TFT_eSPI.h>
#include "SettingsStore.h"
#include "Touchscreen.h"
#include "shared.h"

extern TFT_eSPI tft;

#define TOUCHCAL_NS   "touchcal"
#define TOUCHCAL_VER  1

#ifndef TOUCHCAL_AUTO_FIRST_BOOT
#if defined(BOARD_CYD)
#define TOUCHCAL_AUTO_FIRST_BOOT 1
#else
#define TOUCHCAL_AUTO_FIRST_BOOT 0
#endif
#endif

#ifndef TOUCHCAL_HOLD_TO_RECAL_MS
#define TOUCHCAL_HOLD_TO_RECAL_MS 1200
#endif

/* ───────────────────────── NVS ───────────────────────── */

static bool spanSane(uint16_t a, uint16_t b) {
  if (a > 4095 || b > 4095) return false;
  return abs((int)b - (int)a) >= 1200;
}

bool touchCalLoadNvs() {
  Preferences p;
  if (!p.begin(TOUCHCAL_NS, true)) {      // namespace belum ada
    return false;
  }

  bool ok = false;
  if (p.getUChar("ver", 0) == TOUCHCAL_VER && p.getBool("valid", false)) {
    String board = p.getString("board", "");
    uint8_t rot  = p.getUChar("rot", 255);
    if (board == TOUCH_PROFILE_ID && rot == (uint8_t)TFT_ROTATION) {
      uint16_t xMin = p.getUShort("xmin", 0);
      uint16_t xMax = p.getUShort("xmax", 0);
      uint16_t yMin = p.getUShort("ymin", 0);
      uint16_t yMax = p.getUShort("ymax", 0);
      if (spanSane(xMin, xMax) && spanSane(yMin, yMax)) {
        auto& s = settings();
        s.touchXMin = xMin;
        s.touchXMax = xMax;
        s.touchYMin = yMin;
        s.touchYMax = yMax;
        ok = true;
      }
    }
  }
  p.end();
  return ok;
}

bool touchCalSaveNvs() {
  Preferences p;
  if (!p.begin(TOUCHCAL_NS, false)) {
    return false;
  }
  auto& s = settings();
  bool ok = true;
  p.putBool("valid", false);               // tandai tidak valid selama menulis
  ok &= p.putUShort("xmin", s.touchXMin) > 0;
  ok &= p.putUShort("xmax", s.touchXMax) > 0;
  ok &= p.putUShort("ymin", s.touchYMin) > 0;
  ok &= p.putUShort("ymax", s.touchYMax) > 0;
  ok &= p.putString("board", TOUCH_PROFILE_ID) > 0;
  ok &= p.putUChar("rot", (uint8_t)TFT_ROTATION) > 0;
  ok &= p.putUChar("ver", TOUCHCAL_VER) > 0;
  if (ok) {
    ok &= p.putBool("valid", true) > 0;
  }
  p.end();
  return ok;
}

bool touchCalClearNvs() {
  Preferences p;
  if (!p.begin(TOUCHCAL_NS, false)) {
    return false;
  }
  bool ok = p.clear();
  p.end();
  return ok;
}

/* ───────────────────── Pengambilan titik ───────────────────── */

static int16_t medianOf(int16_t* v, int n) {
  int16_t t[40];
  if (n > 40) n = 40;
  for (int i = 0; i < n; i++) t[i] = v[i];
  for (int i = 1; i < n; i++) {                 // insertion sort
    int16_t k = t[i];
    int j = i - 1;
    while (j >= 0 && t[j] > k) { t[j + 1] = t[j]; j--; }
    t[j + 1] = k;
  }
  return t[n / 2];
}

/** Tunggu jari benar-benar lepas (stabil 150 ms) agar 1 sentuhan tidak dihitung ganda. */
static void waitRelease() {
  const uint32_t t0 = millis();
  bool quiet = false;
  uint32_t quietSince = 0;
  while (millis() - t0 < 3000) {
    if (isTouchDown(200)) {
      quiet = false;
    } else {
      if (!quiet) { quiet = true; quietSince = millis(); }
      if (millis() - quietSince > 150) return;
    }
    delay(10);
  }
}

/** Tunggu sentuhan, ambil banyak sampel, pakai median. false bila timeout. */
static bool captureRaw(int16_t& ox, int16_t& oy, uint32_t timeoutMs) {
  const int N = 24;
  const uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    int16_t x = 0, y = 0;
    if (!readTouchRawXY(x, y, 300)) {
      delay(10);
      continue;
    }
    delay(80);                                  // biarkan kontak jari stabil

    int16_t xs[N], ys[N];
    int n = 0, misses = 0;
    const uint32_t t1 = millis();
    while (n < N && millis() - t1 < 500) {
      if (readTouchRawXY(x, y, 300)) {
        xs[n] = x;
        ys[n] = y;
        n++;
      } else if (++misses > 3) {
        break;                                  // jari terangkat
      }
      delay(8);
    }
    if (n >= 12) {
      ox = medianOf(xs, n);
      oy = medianOf(ys, n);
      return true;
    }
    waitRelease();                              // tap terlalu singkat -> abaikan
  }
  return false;
}

/* ───────────────────────── Tampilan ───────────────────────── */

static void drawTarget(int x, int y, int step) {
  const int W = tft.width();
  tft.fillScreen(UI_BG);
  tft.drawCircle(x, y, 10, UI_ICON);
  tft.drawCircle(x, y, 4, UI_ICON);
  tft.drawLine(x - 16, y, x + 16, y, UI_ICON);
  tft.drawLine(x, y - 16, x, y + 16, UI_ICON);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(UI_TEXT, UI_BG);
  tft.drawString("Touch Calibration", W / 2, tft.height() / 2 - 14, 2);
  char buf[40];
  snprintf(buf, sizeof(buf), "Tap the target (%d/4)", step);
  tft.drawString(buf, W / 2, tft.height() / 2 + 8, 2);
  tft.setTextDatum(TL_DATUM);
}

static void showMessage(const char* l1, const char* l2, uint16_t color, uint32_t ms) {
  const int W = tft.width();
  const int H = tft.height();
  tft.fillScreen(UI_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(color, UI_BG);
  tft.drawString(l1, W / 2, H / 2 - 12, 2);
  if (l2 && l2[0]) {
    tft.setTextColor(UI_TEXT, UI_BG);
    tft.drawString(l2, W / 2, H / 2 + 12, 2);
  }
  tft.setTextDatum(TL_DATUM);
  delay(ms);
}

/* ───────────────────────── Kalibrasi ───────────────────────── */

bool touchCalRun(bool save) {
  setupTouchscreen();

  const int W = tft.width();
  const int H = tft.height();
  const int m = 20;                              // jarak target dari tepi (px)
  const int pts[4][2] = { {m, m}, {W - m, m}, {W - m, H - m}, {m, H - m} };

  waitRelease();

  for (int attempt = 0; attempt < 3; attempt++) {
    int16_t rx[4], ry[4];

    for (int i = 0; i < 4; i++) {
      drawTarget(pts[i][0], pts[i][1], i + 1);
      if (!captureRaw(rx[i], ry[i], 30000)) {
        showMessage("Calibration timeout", "Using previous values", UI_WARN, 1200);
        return false;
      }
      waitRelease();
    }

    // Rata-rata sisi kiri/kanan dan atas/bawah
    const float xL = (rx[0] + rx[3]) / 2.0f;
    const float xR = (rx[1] + rx[2]) / 2.0f;
    const float yT = (ry[0] + ry[1]) / 2.0f;
    const float yB = (ry[2] + ry[3]) / 2.0f;

    // Titik kiri/kanan (atas/bawah) harus berdekatan; jika tidak -> salah sentuh
    const bool consistent =
        abs(rx[0] - rx[3]) < 700 && abs(rx[1] - rx[2]) < 700 &&
        abs(ry[0] - ry[1]) < 700 && abs(ry[3] - ry[2]) < 700;

    // Ekstrapolasi linear ke tepi layar sebenarnya (x=0..W-1, y=0..H-1),
    // bukan hanya ke titik target yang berjarak m px dari tepi.
    const float sx = (xR - xL) / (float)(W - 2 * m);
    const float sy = (yB - yT) / (float)(H - 2 * m);
    const float rawX0   = xL - sx * m;
    const float rawXmax = xL + sx * (W - 1 - m);
    const float rawYtop = yT - sy * m;
    const float rawYbot = yT + sy * (H - 1 - m);

    auto clampRaw = [](float v) -> uint16_t {
      long r = lroundf(v);
      if (r < 0) r = 0;
      if (r > 4095) r = 4095;
      return (uint16_t)r;
    };

    const uint16_t xMin = clampRaw(rawX0);
    const uint16_t xMax = clampRaw(rawXmax);
    const uint16_t yTop = clampRaw(rawYtop);
    const uint16_t yBot = clampRaw(rawYbot);

    if (!consistent || !spanSane(xMin, xMax) || !spanSane(yTop, yBot)) {
      showMessage("Calibration invalid", "Try again", UI_WARN, 1000);
      continue;
    }

    auto& s = settings();
    s.touchXMin = xMin;
    s.touchXMax = xMax;
#if defined(BOARD_CYD)
    s.touchYMin = yTop;                          // map(rawY, YMin, YMax, 0, H-1)
    s.touchYMax = yBot;
#else
    s.touchYMin = yBot;                          // map(rawY, YMax, YMin, 0, H-1)
    s.touchYMax = yTop;
#endif

    bool ok = true;
    if (save) {
      ok = touchCalSaveNvs();
    }

    char line[48];
    snprintf(line, sizeof(line), "X:%u..%u  Y:%u..%u", xMin, xMax, yTop, yBot);
    showMessage(ok ? "Calibration saved" : "Applied (NVS save FAILED)", line,
                ok ? UI.ok : UI_WARN, 1400);
    Serial.printf("[touchcal] %s %s\n", ok ? "saved" : "NVS save failed", line);
    return true;
  }

  showMessage("Calibration failed", "Using previous values", UI_WARN, 1200);
  return false;
}

/* ───────────────────────── Boot hook ───────────────────────── */

void touchCalBoot() {
  setupTouchscreen();

  const bool have = touchCalLoadNvs();
  Serial.printf("[touchcal] NVS calibration %s\n", have ? "loaded" : "not found");

  if (have) {
#if TOUCHCAL_HOLD_TO_RECAL_MS > 0
    // Tahan layar saat boot -> kalibrasi ulang (berguna bila kalibrasi lama meleset).
    if (isTouchDown(300)) {
      bool held = true;
      const uint32_t t0 = millis();
      while (millis() - t0 < (uint32_t)TOUCHCAL_HOLD_TO_RECAL_MS) {
        if (!isTouchDown(300)) { held = false; break; }
        delay(20);
      }
      if (held) {
        touchCalRun(true);
      }
    }
#endif
    return;
  }

#if TOUCHCAL_AUTO_FIRST_BOOT
  touchCalRun(true);
#endif
}

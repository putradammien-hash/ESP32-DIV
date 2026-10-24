#include "TouchCal.h"
#include "TouchCalMath.h"

#include <Preferences.h>
#include <TFT_eSPI.h>
#include "Touchscreen.h"
#include "shared.h"

extern TFT_eSPI tft;

#define TOUCHCAL_NS   "touchcal"
#define TOUCHCAL_VER  2          // v1 = format min/max lama (tidak kompatibel -> kalibrasi ulang sekali)

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

#define TC_BG   TFT_BLACK
#define TC_FG   TFT_MAGENTA      // warna panah, sama seperti contoh Touch_calibrate TFT_eSPI
#define TC_TEXT TFT_WHITE

/* Parameter aktif (format sama dengan tft.setTouch(calData)) */
static uint16_t s_cal[5] = {0, 0, 0, 0, 0};
static bool     s_calValid = false;

bool touchCalIsValid() {
  return s_calValid;
}

bool touchCalConvert(int16_t rawX, int16_t rawY, int& x, int& y) {
  if (!s_calValid) return false;
  touchCalConvertRaw(s_cal, rawX, rawY, TFT_WIDTH, TFT_HEIGHT, x, y);
  return true;
}

/* ───────────────────────── NVS ───────────────────────── */

bool touchCalLoadNvs() {
  s_calValid = false;

  Preferences p;
  if (!p.begin(TOUCHCAL_NS, true)) {      // namespace belum ada
    return false;
  }

  if (p.getUChar("ver", 0) == TOUCHCAL_VER && p.getBool("valid", false)) {
    String board = p.getString("board", "");
    uint8_t rot  = p.getUChar("rot", 255);
    if (board == TOUCH_PROFILE_ID && rot == (uint8_t)TFT_ROTATION) {
      uint16_t c[5];
      c[0] = p.getUShort("x0", 0);
      c[1] = p.getUShort("xr", 0);
      c[2] = p.getUShort("y0", 0);
      c[3] = p.getUShort("yr", 0);
      c[4] = p.getUShort("flg", 0xFFFF);
      if (touchCalParamsSane(c)) {
        for (int i = 0; i < 5; i++) s_cal[i] = c[i];
        s_calValid = true;
      }
    }
  }
  p.end();
  return s_calValid;
}

bool touchCalSaveNvs() {
  if (!s_calValid) return false;

  Preferences p;
  if (!p.begin(TOUCHCAL_NS, false)) {
    return false;
  }
  p.clear();                               // buang format lama / sisa data
  bool ok = true;
  ok &= p.putUShort("x0",  s_cal[0]) > 0;
  ok &= p.putUShort("xr",  s_cal[1]) > 0;
  ok &= p.putUShort("y0",  s_cal[2]) > 0;
  ok &= p.putUShort("yr",  s_cal[3]) > 0;
  ok &= p.putUShort("flg", s_cal[4]) > 0;
  ok &= p.putString("board", TOUCH_PROFILE_ID) > 0;
  ok &= p.putUChar("rot", (uint8_t)TFT_ROTATION) > 0;
  ok &= p.putUChar("ver", TOUCHCAL_VER) > 0;
  if (ok) {
    ok &= p.putBool("valid", true) > 0;    // ditulis terakhir: data setengah jadi tidak dipakai
  }
  p.end();
  return ok;
}

bool touchCalClearNvs() {
  s_calValid = false;
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
    if (isTouchDown(150)) {
      quiet = false;
    } else {
      if (!quiet) { quiet = true; quietSince = millis(); }
      if (millis() - quietSince > 150) return;
    }
    delay(10);
  }
}

/**
 * Tunggu sentuhan di sudut, ambil banyak sampel dan pakai median.
 * (TFT_eSPI: 8 sampel dirata-rata, threshold dikurangi karena sudut kurang sensitif.)
 */
static bool captureRaw(int16_t& ox, int16_t& oy, uint32_t timeoutMs) {
  const int N = 24;
  const uint16_t Z = 150;                       // lebih rendah: sudut kurang sensitif
  const uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    int16_t x = 0, y = 0;
    if (!readTouchRawXY(x, y, Z)) {
      delay(10);
      continue;
    }
    delay(80);                                  // biarkan kontak jari stabil

    int16_t xs[N], ys[N];
    int n = 0, misses = 0;
    const uint32_t t1 = millis();
    while (n < N && millis() - t1 < 500) {
      if (readTouchRawXY(x, y, Z)) {
        xs[n] = x;
        ys[n] = y;
        n++;
      } else if (++misses > 3) {
        break;                                  // jari terangkat
      }
      delay(8);
    }
    if (n >= 8) {
      ox = medianOf(xs, n);
      oy = medianOf(ys, n);
      return true;
    }
    waitRelease();                              // tap terlalu singkat -> abaikan
  }
  return false;
}

/* ───────────────────────── Tampilan ───────────────────────── */

/** Panah di sudut i (0 kiri-atas, 1 kiri-bawah, 2 kanan-atas, 3 kanan-bawah) — sama dengan TFT_eSPI. */
static void drawCornerArrow(int i) {
  const int W = TFT_WIDTH;
  const int H = TFT_HEIGHT;
  const int sz = 15;

  tft.fillScreen(TC_BG);
  switch (i) {
    case 0:
      tft.drawLine(0, 0, 0, sz, TC_FG);
      tft.drawLine(0, 0, sz, 0, TC_FG);
      tft.drawLine(0, 0, sz, sz, TC_FG);
      break;
    case 1:
      tft.drawLine(0, H - sz - 1, 0, H - 1, TC_FG);
      tft.drawLine(0, H - 1, sz, H - 1, TC_FG);
      tft.drawLine(sz, H - sz - 1, 0, H - 1, TC_FG);
      break;
    case 2:
      tft.drawLine(W - sz - 1, 0, W - 1, 0, TC_FG);
      tft.drawLine(W - sz - 1, sz, W - 1, 0, TC_FG);
      tft.drawLine(W - 1, sz, W - 1, 0, TC_FG);
      break;
    default:
      tft.drawLine(W - sz - 1, H - sz - 1, W - 1, H - 1, TC_FG);
      tft.drawLine(W - 1, H - 1 - sz, W - 1, H - 1, TC_FG);
      tft.drawLine(W - 1 - sz, H - 1, W - 1, H - 1, TC_FG);
      break;
  }

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TC_TEXT, TC_BG);
  tft.drawString("Touch Calibration", W / 2, H / 2 - 14, 2);
  char buf[40];
  snprintf(buf, sizeof(buf), "Tap the corner arrow (%d/4)", i + 1);
  tft.drawString(buf, W / 2, H / 2 + 8, 2);
  tft.setTextDatum(TL_DATUM);
}

static void showMessage(const char* l1, const char* l2, uint16_t color, uint32_t ms) {
  const int W = TFT_WIDTH;
  const int H = TFT_HEIGHT;
  tft.fillScreen(TC_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(color, TC_BG);
  tft.drawString(l1, W / 2, H / 2 - 12, 2);
  if (l2 && l2[0]) {
    tft.setTextColor(TC_TEXT, TC_BG);
    tft.drawString(l2, W / 2, H / 2 + 12, 2);
  }
  tft.setTextDatum(TL_DATUM);
  delay(ms);
}

/* ───────────────────────── Kalibrasi ───────────────────────── */

bool touchCalRun(bool save) {
  setupTouchscreen();
  waitRelease();

  for (int attempt = 0; attempt < 3; attempt++) {
    int32_t v[8] = {0, 0, 0, 0, 0, 0, 0, 0};

    for (int i = 0; i < 4; i++) {
      drawCornerArrow(i);
      int16_t rx = 0, ry = 0;
      if (!captureRaw(rx, ry, 30000)) {
        showMessage("Calibration timeout", "Using previous values", UI_WARN, 1200);
        return false;
      }
      v[i * 2]     = rx;
      v[i * 2 + 1] = ry;
      waitRelease();
    }

    uint16_t params[5];
    if (!touchCalComputeParams(v, params)) {
      showMessage("Calibration invalid", "Try again", UI_WARN, 1000);
      continue;
    }

    for (int i = 0; i < 5; i++) s_cal[i] = params[i];
    s_calValid = true;

    bool ok = true;
    if (save) {
      ok = touchCalSaveNvs();
    }

    char line[48];
    snprintf(line, sizeof(line), "%u %u %u %u f%u", params[0], params[1], params[2], params[3], params[4]);
    showMessage(ok ? "Calibration saved" : "Applied (NVS save FAILED)", line,
                ok ? UI.ok : UI_WARN, 1400);
    Serial.printf("[touchcal] %s calData = { %u, %u, %u, %u, %u }\n",
                  ok ? "saved" : "NVS save failed",
                  params[0], params[1], params[2], params[3], params[4]);
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

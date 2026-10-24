#pragma once
/*
 * Matematika kalibrasi touch — port dari TFT_eSPI (Extensions/Touch.cpp):
 *   TFT_eSPI::calibrateTouch()  -> touchCalComputeParams()
 *   TFT_eSPI::setTouch()/convertRawXY() -> touchCalConvertRaw()
 *
 * Format parameter IDENTIK dengan TFT_eSPI (uint16_t p[5]):
 *   p[0] = x0 (raw minimum sumbu X)     p[1] = rentang raw sumbu X
 *   p[2] = y0 (raw minimum sumbu Y)     p[3] = rentang raw sumbu Y
 *   p[4] = flags: bit0 rotate (sumbu tertukar), bit1 invert X, bit2 invert Y
 *
 * Tanpa dependensi Arduino, jadi bisa diuji di komputer biasa.
 */
#include <stdint.h>
#include <stdlib.h>

#define TOUCHCAL_F_ROTATE   0x01
#define TOUCHCAL_F_INVERT_X 0x02
#define TOUCHCAL_F_INVERT_Y 0x04

/** Cek parameter masuk akal (dipakai saat memuat dari NVS). */
inline bool touchCalParamsSane(const uint16_t p[5]) {
  if (p[0] < 1 || p[0] > 4095) return false;
  if (p[2] < 1 || p[2] > 4095) return false;
  if (p[1] < 1200 || p[1] > 4095) return false;
  if (p[3] < 1200 || p[3] > 4095) return false;
  if (p[4] > 7) return false;
  return true;
}

/**
 * v[8] = raw (x,y) rata-rata di empat sudut layar, urutan sama dengan TFT_eSPI:
 *   v[0],v[1] kiri-atas   v[2],v[3] kiri-bawah
 *   v[4],v[5] kanan-atas  v[6],v[7] kanan-bawah
 * Return false bila sentuhan tidak konsisten (salah sentuh) atau rentang tidak masuk akal.
 */
inline bool touchCalComputeParams(const int32_t v[8], uint16_t p[5]) {
  int32_t x0, x1, y0, y1;
  bool rotate = false, invX = false, invY = false, consistent;

  // Dari kiri-atas ke kiri-bawah (layar turun): bila raw X berubah lebih besar
  // daripada raw Y, sumbu panel dan sumbu TFT tertukar.
  if (abs(v[0] - v[2]) > abs(v[1] - v[3])) {
    rotate = true;
    x0 = (v[1] + v[3]) / 2;
    x1 = (v[5] + v[7]) / 2;
    y0 = (v[0] + v[4]) / 2;
    y1 = (v[2] + v[6]) / 2;
    consistent = abs(v[1] - v[3]) < 700 && abs(v[5] - v[7]) < 700 &&
                 abs(v[0] - v[4]) < 700 && abs(v[2] - v[6]) < 700;
  } else {
    x0 = (v[0] + v[2]) / 2;
    x1 = (v[4] + v[6]) / 2;
    y0 = (v[1] + v[5]) / 2;
    y1 = (v[3] + v[7]) / 2;
    consistent = abs(v[0] - v[2]) < 700 && abs(v[4] - v[6]) < 700 &&
                 abs(v[1] - v[5]) < 700 && abs(v[3] - v[7]) < 700;
  }

  // Arah sumbu panel bisa berlawanan dengan sumbu TFT.
  if (x0 > x1) { int32_t t = x0; x0 = x1; x1 = t; invX = true; }
  if (y0 > y1) { int32_t t = y0; y0 = y1; y1 = t; invY = true; }

  // Pra-hitung: simpan rentang, bukan nilai maksimum.
  x1 -= x0;
  y1 -= y0;

  if (x0 == 0) x0 = 1;
  if (x1 == 0) x1 = 1;
  if (y0 == 0) y0 = 1;
  if (y1 == 0) y1 = 1;

  if (!consistent) return false;
  if (x0 < 0 || y0 < 0 || x0 > 4095 || y0 > 4095 || x1 > 4095 || y1 > 4095) return false;

  p[0] = (uint16_t)x0;
  p[1] = (uint16_t)x1;
  p[2] = (uint16_t)y0;
  p[3] = (uint16_t)y1;
  p[4] = (uint16_t)((rotate ? TOUCHCAL_F_ROTATE : 0) |
                    (invX ? TOUCHCAL_F_INVERT_X : 0) |
                    (invY ? TOUCHCAL_F_INVERT_Y : 0));
  return touchCalParamsSane(p);
}

/** Raw -> piksel layar (rumus convertRawXY TFT_eSPI, ditambah clamp ke ukuran layar). */
inline void touchCalConvertRaw(const uint16_t p[5], int32_t rawX, int32_t rawY,
                               int W, int H, int& x, int& y) {
  const bool rotate = p[4] & TOUCHCAL_F_ROTATE;
  const bool invX   = p[4] & TOUCHCAL_F_INVERT_X;
  const bool invY   = p[4] & TOUCHCAL_F_INVERT_Y;

  const int32_t a = rotate ? rawY : rawX;
  const int32_t b = rotate ? rawX : rawY;

  int32_t xx = (a - (int32_t)p[0]) * W / (int32_t)p[1];
  int32_t yy = (b - (int32_t)p[2]) * H / (int32_t)p[3];

  if (invX) xx = W - xx;
  if (invY) yy = H - yy;

  if (xx < 0) xx = 0;
  if (xx > W - 1) xx = W - 1;
  if (yy < 0) yy = 0;
  if (yy > H - 1) yy = H - 1;

  x = (int)xx;
  y = (int)yy;
}

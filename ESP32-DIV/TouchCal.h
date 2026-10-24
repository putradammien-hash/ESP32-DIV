#pragma once
/*
 * TouchCal — kalibrasi layar sentuh XPT2046 ala TFT_eSPI (Touch_calibrate) + NVS.
 *
 * Mekanisme mengikuti TFT_eSPI::calibrateTouch()/setTouch():
 *   - 4 sudut layar sebenarnya (panah kecil di tiap sudut)
 *   - 5 parameter {x0, rentangX, y0, rentangY, flags(rotate/invertX/invertY)}
 *   - deteksi otomatis sumbu tertukar / terbalik
 * Bedanya: parameter disimpan di NVS (bukan SPIFFS) dan raw dibaca lewat
 * XPT2046_Touchscreen karena touch CYD ada di bus SPI terpisah.
 *
 * - Boot pertama (NVS kosong) pada CYD: kalibrasi otomatis.
 * - Tahan layar ~1.2 detik saat boot untuk memaksa kalibrasi ulang.
 * - Menu Tools -> "Touch Calibrate" tetap bisa dipakai kapan saja.
 *
 * Opsi (boleh di-define di BoardConfig.h):
 *   TOUCHCAL_AUTO_FIRST_BOOT   1/0  kalibrasi otomatis bila NVS kosong (default 1 di CYD)
 *   TOUCHCAL_HOLD_TO_RECAL_MS  ms   tahan layar saat boot untuk kalibrasi ulang (0 = nonaktif)
 */

#include <Arduino.h>

/** Muat 5 parameter kalibrasi dari NVS. true bila data valid dan dipakai. */
bool touchCalLoadNvs();

/** Simpan parameter kalibrasi aktif ke NVS. */
bool touchCalSaveNvs();

/** Hapus kalibrasi dari NVS dan dari memori (boot berikutnya kalibrasi ulang). */
bool touchCalClearNvs();

/** True bila ada kalibrasi valid yang aktif. */
bool touchCalIsValid();

/** Konversi raw -> piksel layar memakai kalibrasi aktif. false bila belum ada kalibrasi. */
bool touchCalConvert(int16_t rawX, int16_t rawY, int& x, int& y);

/** Kalibrasi 4 sudut (blocking). Bila save=true hasilnya ditulis ke NVS. true bila sukses. */
bool touchCalRun(bool save = true);

/** Panggil sekali di setup(): init touch, muat NVS, auto-kalibrasi / hold-to-recalibrate. */
void touchCalBoot();

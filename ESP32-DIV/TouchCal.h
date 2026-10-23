#pragma once
/*
 * TouchCal — kalibrasi layar sentuh XPT2046 (4 titik) + penyimpanan di NVS.
 *
 * - Hasil kalibrasi disimpan di NVS (namespace "touchcal"), jadi tetap ada
 *   setelah reboot / power-off dan TIDAK bergantung pada SD card.
 * - Boot pertama (belum ada data NVS) pada board CYD: kalibrasi otomatis.
 * - Tahan layar ~1.2 detik saat boot untuk memaksa kalibrasi ulang.
 * - Menu Tools -> "Touch Calibrate" tetap bisa dipakai kapan saja.
 *
 * Opsi (boleh di-define di BoardConfig.h):
 *   TOUCHCAL_AUTO_FIRST_BOOT   1/0  kalibrasi otomatis bila NVS kosong (default 1 di CYD)
 *   TOUCHCAL_HOLD_TO_RECAL_MS  ms   tahan layar saat boot untuk kalibrasi ulang (0 = nonaktif)
 */

#include <Arduino.h>

/** Muat kalibrasi dari NVS ke settings(). true bila data valid dan dipakai. */
bool touchCalLoadNvs();

/** Simpan nilai kalibrasi yang ada di settings() ke NVS. */
bool touchCalSaveNvs();

/** Hapus kalibrasi dari NVS (boot berikutnya memakai default / kalibrasi ulang). */
bool touchCalClearNvs();

/** Kalibrasi 4 titik (blocking). Bila save=true hasilnya ditulis ke NVS. true bila sukses. */
bool touchCalRun(bool save = true);

/** Panggil sekali di setup(): init touch, muat NVS, auto-kalibrasi / hold-to-recalibrate. */
void touchCalBoot();

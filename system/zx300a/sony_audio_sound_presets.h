#ifndef WALKDROID_SONY_SOUND_PRESETS_H
#define WALKDROID_SONY_SOUND_PRESETS_H
/* Pure projection of the original ZX300A/WM1Z-capability sound presets.
 * Audited original GUI UserPresetNo=0..2 corresponds to public slots=1..3.
 * SAVE/LOAD copies exactly 26 original sound fields and reseals the original
 * audio snapshot CRC. Volume, gain, AVLS, balance, DSD, direct, BT effects and
 * the independent CFW profile/gain records are not preset fields.
 *
 * No file/device access, IPC, DSP commands, ownership or apply receipt here.
 * The caller must serialize, validate the supported control domains, stage
 * real original DSP controls, verify native getters and commit with the
 * existing expected-snapshot CAS before presenting a successful operation.
 * This helper alone is not a production preset service or UI implementation.
 */
#define SONY_SOUND_PRESET_SAVE 1u
#define SONY_SOUND_PRESET_LOAD 2u
#define SONY_SOUND_PRESET_FIELDS 26u
int sony_sound_preset_project(const void *snapshot,unsigned int size,
    unsigned int operation,unsigned int slot,void *output);
/* Check the selected original bank's 26 source cells. An uninitialized cell
 * (0x0000fefe) returns -ENODATA; no invented defaults are substituted. */
int sony_sound_preset_available(const void *snapshot,unsigned int size,unsigned int slot);
#endif

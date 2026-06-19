#ifndef FLUX_VOICE_H
#define FLUX_VOICE_H

#include <stddef.h>

/* Returns 1 if arecord or ffmpeg is present (recording possible). */
int flux_voice_can_record(void);

/* Returns 1 if a whisper-cli binary and model file are found. */
int flux_voice_can_transcribe(void);

/* Starts background recording via arecord into /tmp/flux_voice.wav.
 * Returns 1 on success, 0 on failure. Non-blocking. */
int flux_voice_start(void);

/* Stops the background recording process, runs Whisper on the WAV file,
 * writes the recognised German text into out (max out_cap bytes).
 * Blocks for ~1-5 seconds while Whisper runs.
 * Returns 1 if non-empty text was recognised, 0 otherwise. */
int flux_voice_stop_and_transcribe(char *out, size_t out_cap);

/* Returns 1 while recording is in progress. */
int flux_voice_is_recording(void);

/* Hard-stops recording without transcribing (e.g. on screen change). */
void flux_voice_cancel(void);

#endif

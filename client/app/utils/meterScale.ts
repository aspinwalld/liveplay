/**
 * Geometry shared by the mixer's faders, meters and the single dB scale that
 * sits between them.
 *
 * The scale only works for both because the fader and the meter map dB to
 * position the *same* way — linearly across their range, no taper. If either
 * ever gains a taper, one scale stops being honest for both and they need
 * separate scales again.
 *
 * The fader spans the full range; the meter is dBFS and cannot exceed 0, so
 * its track is only as tall as that portion. That puts 0 dBFS exactly on the
 * 0 tick, keeps the meter's own resolution intact, and leaves no dead strip
 * above the meter that a signal could never reach.
 */
export const FADER_MIN_DB = -60;
export const FADER_MAX_DB = 12;
export const METER_MAX_DB = 0;

/** Meter track height, as a percentage of the fader's travel. */
export const METER_TRACK_PCT =
  ((METER_MAX_DB - FADER_MIN_DB) / (FADER_MAX_DB - FADER_MIN_DB)) * 100;

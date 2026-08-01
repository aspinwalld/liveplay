<template>
  <!--
    Four-band EQ.

    The grid runs bands across and parameters down: one column per band, rows
    for frequency, gain and Q. That way a column is a band — the thing you
    actually reach for — and comparing the same parameter across bands is a
    glance along a row. The transpose (parameters across, bands down) reads
    worse for both.

    Every cell is a knob and a typeable box, because an EQ set by ear and an EQ
    set from a spec sheet are both real jobs.

    Shell until the DSP stage lands: the panel is dashed and labelled, and its
    controls are disabled, so nothing here can be mistaken for something that
    is processing audio.
  -->
  <section class="eq det__panel det__panel--pending">
    <h4 class="det__h">
      {{ t('mixer.tabEq') }}
      <span class="det__pending">{{ t('mixer.notImplemented') }}</span>
    </h4>

    <!-- Response curve. Flat, with a handle per band at its frequency —
         the shape the real curve will take, so the panel does not change
         layout when it starts working. This is the part that grows into
         spare height; the band controls below it are never squeezed. -->
    <div class="eq__graph">
      <svg viewBox="0 0 400 120" preserveAspectRatio="none" class="eq__svg">
        <line
          v-for="g in gridDb" :key="'g' + g"
          class="eq__grid" x1="0" x2="400" :y1="yFor(g)" :y2="yFor(g)"
        />
        <line class="eq__zero" x1="0" x2="400" :y1="yFor(0)" :y2="yFor(0)" />
        <polyline class="eq__curve" :points="curvePoints" />
      </svg>
      <span
        v-for="(b, i) in bands" :key="'h' + b.id"
        class="eq__handle"
        :style="{ left: xPctFor(b.freq) + '%', background: handleColor(i) }"
      >{{ b.id }}</span>
    </div>

    <div class="eq__grid-controls">
      <!-- Header row: the bands. -->
      <span class="eq__rowlabel"></span>
      <span v-for="b in bands" :key="'n' + b.id" class="eq__bandname">{{ b.id }}</span>

      <span class="eq__rowlabel">{{ t('mixer.freq') }}</span>
      <KnobField
        v-for="b in bands" :key="'f' + b.id"
        :value="b.freq" :min="20" :max="20000" :origin="1000"
        :decimals="0" unit="Hz" :size="34" :show-label="false" disabled
      />

      <span class="eq__rowlabel">{{ t('mixer.gain') }}</span>
      <KnobField
        v-for="b in bands" :key="'g' + b.id"
        :value="b.gain" :min="-18" :max="18" :origin="0"
        :decimals="1" unit="dB" :size="34" :show-label="false" disabled
      />

      <span class="eq__rowlabel">{{ t('mixer.q') }}</span>
      <KnobField
        v-for="b in bands" :key="'q' + b.id"
        :value="b.q" :min="0.1" :max="10" :origin="0.7"
        :decimals="2" :size="34" :show-label="false" disabled
      />
    </div>
  </section>
</template>

<script setup lang="ts">
import { computed } from 'vue';
import KnobField from './KnobField.vue';
import { METER_COLORS } from '~/composables/useOutputTarget';

const { t } = useLocalization();

// Default band layout. These are the values the panel displays until the DSP
// stage gives it real ones — chosen as a conventional four-band starting point
// rather than invented.
const bands = [
  { id: 'LF',  freq: 100,   gain: 0, q: 0.7 },
  { id: 'LMF', freq: 500,   gain: 0, q: 1.0 },
  { id: 'HMF', freq: 2500,  gain: 0, q: 1.0 },
  { id: 'HF',  freq: 10000, gain: 0, q: 0.7 },
];

const GRAPH_DB = 18;               // curve spans +/- this
const gridDb = [12, 6, -6, -12];

// Frequency axis is logarithmic, as an EQ display always is: an octave takes
// the same width everywhere, so 100-200 Hz reads as wide as 1-2 kHz.
const LO = Math.log10(20);
const HI = Math.log10(20000);
const xPctFor = (hz: number) => ((Math.log10(hz) - LO) / (HI - LO)) * 100;
const yFor = (db: number) => 60 - (db / GRAPH_DB) * 60;

// Flat, because every band is at 0 dB. Drawn as a polyline rather than a
// straight line so the real response can replace the points and nothing else.
const curvePoints = computed(() =>
  Array.from({ length: 41 }, (_, i) => `${i * 10},${yFor(0)}`).join(' '));

const handleColor = (i: number) =>
  [METER_COLORS.blue, METER_COLORS.green, METER_COLORS.yellow, METER_COLORS.red][i]
  ?? METER_COLORS.green;
</script>

<style scoped>
/* The controls never clip: they keep their natural height and the graph
   absorbs whatever is left. If the panel still cannot fit, the work area
   scrolls rather than the knobs disappearing off the bottom — an EQ you can
   see but not adjust is worse than one you have to scroll to. */
.eq { min-height: 0; }
.eq > *:not(.eq__graph) { flex: 0 0 auto; }

.eq__graph {
  position: relative;
  flex: 1 1 auto;
  min-height: 120px;
  background: var(--color-background);
  border-radius: var(--border-radius-sm);
  overflow: hidden;
}
.eq__svg { display: block; width: 100%; height: 100%; }
.eq__grid  { stroke: var(--color-border); stroke-width: 1; opacity: 0.5; }
.eq__zero  { stroke: var(--color-text-disabled); stroke-width: 1; }
.eq__curve { fill: none; stroke: var(--color-accent); stroke-width: 2; }

.eq__handle {
  position: absolute;
  top: 50%;
  transform: translate(-50%, -50%);
  font-size: 8px;
  font-family: var(--font-mono);
  line-height: 1;
  padding: 2px 3px;
  border-radius: 2px;
  color: #000;
  opacity: 0.85;
  pointer-events: none;
}

/* Bands across, parameters down. auto-flow row so each declaration block above
   fills one row left to right. */
.eq__grid-controls {
  display: grid;
  grid-template-columns: auto repeat(4, 1fr);
  align-items: center;
  justify-items: center;
  gap: 4px 2px;
}
.eq__rowlabel {
  justify-self: start;
  font-size: 9px;
  letter-spacing: 0.04em;
  text-transform: uppercase;
  color: var(--color-text-secondary);
}
.eq__bandname {
  font-family: var(--font-mono);
  font-size: 10px;
  color: var(--color-text-primary);
}
</style>

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

    The curve is live and includes the channel's high- and low-pass, which live
    on the fader column but shape this same signal — an EQ display that ignored
    them would be drawing a lie. The four band controls are still shells, so
    the pending badge sits on them rather than on the whole panel.
  -->
  <section class="eq det__panel">
    <h4 class="det__h">{{ t('mixer.tabEq') }}</h4>

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
        <!-- The grid lines are stretched with the box too; pinning their width
             the same way stops them thickening as the panel grows. -->
        <polyline class="eq__curve" :points="curvePoints" />
      </svg>
      <span
        v-for="(b, i) in bands" :key="'h' + b.id"
        class="eq__handle"
        :style="{ left: xPctFor(b.freq) + '%', background: handleColor(i) }"
      >{{ b.id }}</span>
      <!-- The filters get markers too, so a corner in the curve can be traced
           to the knob that put it there rather than looking like an EQ band
           nobody moved. Only shown when the filter is in circuit. -->
      <span
        v-if="hpfIn"
        class="eq__handle eq__handle--filter"
        :style="{ left: xPctFor(bus!.dsp.hpf.freq) + '%' }"
      >{{ t('mixer.hpf') }}</span>
      <span
        v-if="lpfIn"
        class="eq__handle eq__handle--filter"
        :style="{ left: xPctFor(bus!.dsp.lpf.freq) + '%' }"
      >{{ t('mixer.lpf') }}</span>
    </div>

    <div class="eq__grid-controls eq__grid-controls--pending">
      <span class="eq__pending">{{ t('mixer.notImplemented') }}</span>
      <!-- Header row: the bands. -->
      <span class="eq__rowlabel"></span>
      <span v-for="b in bands" :key="'n' + b.id" class="eq__bandname">{{ b.id }}</span>

      <span class="eq__rowlabel">{{ t('mixer.freq') }}</span>
      <KnobField
        v-for="b in bands" :key="'f' + b.id"
        :value="b.freq" :min="20" :max="20000" :origin="1000"
        :decimals="0" unit="Hz" :size="30" :show-label="false" disabled
      />

      <span class="eq__rowlabel">{{ t('mixer.gain') }}</span>
      <KnobField
        v-for="b in bands" :key="'g' + b.id"
        :value="b.gain" :min="-18" :max="18" :origin="0"
        :decimals="1" unit="dB" :size="30" :show-label="false" disabled
      />

      <span class="eq__rowlabel">{{ t('mixer.q') }}</span>
      <KnobField
        v-for="b in bands" :key="'q' + b.id"
        :value="b.q" :min="0.1" :max="10" :origin="0.7"
        :decimals="2" :size="30" :show-label="false" disabled
      />
    </div>
  </section>
</template>

<script setup lang="ts">
import { computed } from 'vue';
import KnobField from './KnobField.vue';
import { METER_COLORS } from '~/composables/useOutputTarget';
import type { Bus } from '~/types/project';
import { HPF_PARKED_HZ, LPF_PARKED_HZ } from '~/types/project';
import type { BiquadCoeffs } from '~/utils/filterResponse';
import {
  biquadHighpass, biquadLowpass, biquadPeaking, combinedMagnitudeDb,
} from '~/utils/filterResponse';

// The bus whose curve this is. Optional so the panel still renders (flat)
// before the first bus fetch lands.
const props = defineProps<{ bus?: Bus | null }>();

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

// The curve, including the channel's high- and low-pass.
//
// The filters live on the fader column, not in this panel, but they shape the
// same signal and an EQ display that ignored them would be drawing a lie: turn
// a 400 Hz high-pass in and the bottom of the band goes with it, whatever the
// LF band says. Every section that touches the audio belongs in the picture.
//
// The maths mirrors the C++ engine (see utils/filterResponse.ts). It is a
// second model of the same filters, so it is display-only and the two have to
// be kept in step.
const SAMPLE_RATE = 48000;

const sections = computed<BiquadCoeffs[]>(() => {
  const out: BiquadCoeffs[] = [];
  const hpf = props.bus?.dsp?.hpf;
  const lpf = props.bus?.dsp?.lpf;
  // Parked at the end of its travel is out of circuit — the same rule the
  // server applies when it decides whether to run the section at all.
  if (hpf && hpf.freq > HPF_PARKED_HZ) {
    out.push(biquadHighpass(hpf.freq, SAMPLE_RATE, hpf.q || 0.7071));
  }
  if (lpf && lpf.freq < LPF_PARKED_HZ) {
    out.push(biquadLowpass(lpf.freq, SAMPLE_RATE, lpf.q || 0.7071));
  }
  for (const b of bands) {
    if (b.gain !== 0) out.push(biquadPeaking(b.freq, SAMPLE_RATE, b.gain, b.q));
  }
  return out;
});

// Sampled along the same log axis the handles sit on, so a corner lands under
// its knob rather than a few pixels off it.
const CURVE_POINTS = 81;
const curvePoints = computed(() => {
  const secs = sections.value;
  return Array.from({ length: CURVE_POINTS }, (_, i) => {
    const x  = (i / (CURVE_POINTS - 1)) * 400;
    const hz = Math.pow(10, LO + (i / (CURVE_POINTS - 1)) * (HI - LO));
    const db = secs.length ? combinedMagnitudeDb(secs, hz, SAMPLE_RATE) : 0;
    // Clamped to the drawn range: a 24 dB/octave skirt heads for -80 dB and
    // would otherwise draw a vertical spike off the bottom of the box.
    const clamped = Math.max(-GRAPH_DB, Math.min(GRAPH_DB, db));
    return `${x.toFixed(1)},${yFor(clamped).toFixed(1)}`;
  }).join(' ');
});

const hpfIn = computed(() =>
  !!props.bus?.dsp?.hpf && props.bus.dsp.hpf.freq > HPF_PARKED_HZ);
const lpfIn = computed(() =>
  !!props.bus?.dsp?.lpf && props.bus.dsp.lpf.freq < LPF_PARKED_HZ);

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

/* The band controls are still shells; the curve above them is not. The dashed
   frame is on the controls alone so the panel does not disown a display that
   is telling the truth. */
.eq__grid-controls--pending {
  position: relative;
  border: 1px dashed var(--color-border);
  border-radius: var(--border-radius-sm);
  padding-top: 10px;
}
.eq__pending {
  position: absolute;
  top: 1px;
  right: 4px;
  font-size: 8px;
  color: var(--color-text-disabled);
}
/* Filter markers read as fixtures rather than as a fifth and sixth band. */
.eq__handle--filter {
  background: var(--color-text-disabled);
  opacity: 0.85;
}

/* EQ owns a full-height column of its own now, so the graph goes back to
   taking whatever the band controls below it do not. It was pinned to a
   clamped height when EQ shared a row with dynamics — growing it there stole
   from the panels underneath, which is no longer true. A curve display is one
   of the few things that genuinely keeps improving with height, so there is no
   ceiling; the floor is what stops it collapsing on a short window. */
.eq__graph {
  position: relative;
  flex: 1 1 auto;
  min-height: 140px;
  background: var(--color-background);
  border-radius: var(--border-radius-sm);
  overflow: hidden;
}
.eq__svg { display: block; width: 100%; height: 100%; }
.eq__grid  {
  stroke: var(--color-border);
  stroke-width: 1;
  vector-effect: non-scaling-stroke;
  opacity: 0.5;
}
.eq__zero  {
  stroke: var(--color-text-disabled);
  stroke-width: 1;
  vector-effect: non-scaling-stroke;
}
/* non-scaling-stroke matters here. The viewBox is stretched to the panel with
   preserveAspectRatio="none", so an ordinary stroke is scaled with it and
   renders far heavier than the number suggests. This pins the line to a real
   device width, which is also what keeps it even as the panel resizes. */
.eq__curve {
  fill: none;
  stroke: var(--color-accent);
  stroke-width: 1.25;
  vector-effect: non-scaling-stroke;
  stroke-linejoin: round;
}

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

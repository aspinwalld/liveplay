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
      <!-- A handle sits at its band's frequency and rides its gain, so the
           marker is on the part of the curve it made. -->
      <span
        v-for="(b, i) in bands" :key="'h' + i"
        class="eq__handle"
        :class="{ 'eq__handle--out': b.gain === 0 }"
        :style="{
          left: xPctFor(b.freq) + '%',
          top: (yFor(Math.max(-GRAPH_DB, Math.min(GRAPH_DB, b.gain))) / 120 * 100) + '%',
          background: handleColor(i),
        }"
      >{{ EQ_BAND_NAMES[i] }}</span>
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

    <div class="eq__grid-controls">
      <!-- Header row: the bands. A band's name lights while it is in circuit,
           which for a bell means its gain is off zero — the same rule the
           engine uses to decide whether to run the section at all. -->
      <span class="eq__rowlabel"></span>
      <span
        v-for="(b, i) in bands" :key="'n' + i"
        class="eq__bandname"
        :class="{ 'eq__bandname--in': b.gain !== 0 }"
      >{{ EQ_BAND_NAMES[i] }}</span>

      <!-- Frequency is logarithmic: what the ear hears is the ratio between
           two frequencies, so an octave should take the same arc wherever it
           sits. On a linear taper 1 kHz would land at 5% of travel. -->
      <span class="eq__rowlabel">{{ t('mixer.freq') }}</span>
      <KnobField
        v-for="(b, i) in bands" :key="'f' + i"
        :value="b.freq" :min="20" :max="20000" :origin="bandDefaults[i]!.freq"
        taper="log"
        :decimals="0" unit="Hz" :size="30" :show-label="false"
        :disabled="!bus"
        @input="(v: number) => onBand(i, 'freq', v)"
      />

      <span class="eq__rowlabel">{{ t('mixer.gain') }}</span>
      <KnobField
        v-for="(b, i) in bands" :key="'g' + i"
        :value="b.gain" :min="-18" :max="18" :origin="0"
        :decimals="1" unit="dB" :size="30" :show-label="false"
        :disabled="!bus"
        @input="(v: number) => onBand(i, 'gain', v)"
      />

      <!-- Q is logarithmic too: 0.5 to 1 is the same change of shape as 4 to
           8, and a linear taper would waste most of the dial above Q 3 where
           the differences stop being audible. -->
      <span class="eq__rowlabel">{{ t('mixer.q') }}</span>
      <KnobField
        v-for="(b, i) in bands" :key="'q' + i"
        :value="b.q" :min="0.1" :max="10" :origin="bandDefaults[i]!.q"
        taper="log"
        :decimals="2" :size="30" :show-label="false"
        :disabled="!bus"
        @input="(v: number) => onBand(i, 'q', v)"
      />
    </div>
  </section>
</template>

<script setup lang="ts">
import { computed, onBeforeUnmount, ref, watch } from 'vue';
import KnobField from './KnobField.vue';
import { METER_COLORS } from '~/composables/useOutputTarget';
import type { Bus, BusDsp, BusEqBand } from '~/types/project';
import { EQ_BAND_NAMES, HPF_PARKED_HZ, LPF_PARKED_HZ } from '~/types/project';
import type { BiquadCoeffs } from '~/utils/filterResponse';
import {
  biquadHighpass, biquadLowpass, biquadPeaking, combinedMagnitudeDb,
} from '~/utils/filterResponse';

// The bus whose curve this is. Optional so the panel still renders (flat)
// before the first bus fetch lands.
const props = defineProps<{ bus?: Bus | null }>();

const emit = defineEmits<{
  (e: 'patch', id: string, patch: Partial<Bus>): void;
  /** In-flight band values, so the curve tracks the knob. See the fader. */
  (e: 'dsp-live', dsp: Partial<BusDsp>): void;
}>();

const { t } = useLocalization();
const server = useLiveplayServer();

// The conventional four-band starting layout. Doubles as each knob's origin,
// so a double-click puts a band back where it started rather than at zero
// Hertz — and it must match the server's defaults or a fresh bus would appear
// to have been moved already.
const bandDefaults: BusEqBand[] = [
  { freq: 100,   gain: 0, q: 0.7 },
  { freq: 500,   gain: 0, q: 1.0 },
  { freq: 2500,  gain: 0, q: 1.0 },
  { freq: 10000, gain: 0, q: 0.7 },
];

// The bands as displayed: the bus's, held locally while a knob is moving.
//
// Same live-then-persist shape as the filters and the fader. The strip gets
// new coefficients on every drag event over a call that writes no document,
// and the bus is written once the gesture settles. Binding straight to the bus
// would PATCH and refetch per event, and the knob would fight the round trip.
const localBands = ref<BusEqBand[] | null>(null);
let   bandHold   = false;
let   bandSettle: ReturnType<typeof setTimeout> | null = null;

const bands = computed<BusEqBand[]>(() =>
  localBands.value
  ?? props.bus?.dsp?.eq
  ?? bandDefaults);

watch(() => props.bus?.dsp?.eq, () => { if (!bandHold) localBands.value = null; },
      { deep: true });
watch(() => props.bus?.id, () => { localBands.value = null; });

onBeforeUnmount(() => { if (bandSettle) clearTimeout(bandSettle); });

function onBand(index: number, key: keyof BusEqBand, value: number) {
  if (!props.bus) return;
  const next = bands.value.map((b, i) => (i === index ? { ...b, [key]: value } : { ...b }));
  localBands.value = next;
  bandHold = true;
  // The curve is told first, so it tracks the knob rather than the round trip.
  emit('dsp-live', { eq: next });
  void server.setBusDsp(props.bus.id, { eq: next }).catch(() => {});
  if (bandSettle) clearTimeout(bandSettle);
  bandSettle = setTimeout(() => {
    bandSettle = null;
    bandHold   = false;
    emit('patch', props.bus!.id, { dsp: { eq: next } } as Partial<Bus>);
  }, 250);
}

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
  for (const b of bands.value) {
    // 0 dB is an identity whatever the Q, so it contributes nothing to draw.
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

/* A band out of circuit still shows its handle, so you can find it to bring it
   in, but it recedes rather than competing with the bands doing something. */
.eq__handle--out { opacity: 0.4; }
.eq__bandname--in { color: var(--color-accent); }
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

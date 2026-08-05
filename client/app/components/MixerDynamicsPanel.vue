<template>
  <!--
    Dynamics: an expander/gate and a compressor/limiter.

    Left to right: the transfer graph at full height, then a gain-reduction
    meter for each processor, then their controls. The graph is shared because
    both act on the same axis — input level in, output level out — and one
    curve is how you see what the two together do to a signal; two graphs would
    show two halves of one answer. The GR meters are not shared, because how
    much each one is pulling is exactly what you need to tell them apart.

    Shell until the DSP stage lands: dashed, labelled, controls disabled.
  -->
  <section class="dyn det__panel" :class="{ 'dyn--bypassed': !dynIn }">
    <h4 class="det__h">
      {{ t('mixer.tabDynamics') }}
      <button
        class="det__byp"
        :class="{ 'det__byp--on': !dynIn }"
        :disabled="!bus"
        :title="t('mixer.bypassHint')"
        @click="toggleSection"
      >{{ t('mixer.bypass') }}</button>
    </h4>

    <div class="dyn__body">
      <!-- Transfer curve: input level across, output level down. Unity is the
           diagonal; the gate pulls the bottom-left down and the compressor
           flattens the top-right. Flat-unity until there is something to
           plot. -->
      <div class="dyn__graph">
        <svg viewBox="0 0 120 120" preserveAspectRatio="none" class="dyn__svg">
          <line v-for="g in [30, 60, 90]" :key="'v' + g" class="dyn__grid" :x1="g" :x2="g" y1="0" y2="120" />
          <line v-for="g in [30, 60, 90]" :key="'h' + g" class="dyn__grid" x1="0" x2="120" :y1="g" :y2="g" />
          <line class="dyn__unity" x1="0" y1="120" x2="120" y2="0" />
        </svg>
      </div>

      <!-- Gain reduction, one per processor. Deliberately not StereoMeter:
           that measures signal level against the project's output target,
           and this measures how far a processor is pulling down — a different
           quantity on a different scale. Empty until the processors exist. -->
      <div class="dyn__grmeters">
        <!-- The gate's is live: it fills downward from the top by how far the
             processor is pulling, which is the direction gain reduction
             actually moves. The label doubles as the processor's in/out,
             separate from the section bypass in the header — a strip can have
             dynamics in with only one of the two working. -->
        <div class="dyn__gr">
          <div class="dyn__grtrack">
            <div class="dyn__grfill" :style="{ height: gateGrPct + '%' }"></div>
          </div>
          <button
            class="dyn__grlabel dyn__grlabel--btn"
            :class="{ 'dyn__grlabel--on': gateOn }"
            :disabled="!bus"
            :title="t('mixer.gateToggle')"
            @click="toggleGate"
          >{{ t('mixer.gateShort') }}</button>
        </div>
        <div class="dyn__gr">
          <div class="dyn__grtrack"></div>
          <span class="dyn__grlabel">{{ t('mixer.compShort') }}</span>
        </div>
      </div>

      <div class="dyn__controls">
        <div class="dyn__group" :class="{ 'dyn__group--out': !gateOn }">
          <h5 class="dyn__h">{{ t('mixer.gate') }}</h5>
          <div class="dyn__row">
            <KnobField
              v-for="p in gateParams" :key="p.field"
              :value="gateValues[p.field]" :min="p.min" :max="p.max" :origin="p.origin"
              :taper="p.taper" :decimals="p.decimals" :unit="p.unit" :label="t(p.key)"
              :size="28" :disabled="!bus"
              @input="(v: number) => onGate(p.field, v)"
            />
          </div>
        </div>

        <!-- Still a shell. The badge sits on this half alone now that the gate
             beside it is real; the panel should not disown a processor that
             works. -->
        <div class="dyn__group dyn__group--pending">
          <h5 class="dyn__h">
            {{ t('mixer.compressor') }}
            <span class="det__pending">{{ t('mixer.notImplemented') }}</span>
          </h5>
          <div class="dyn__row">
            <KnobField
              v-for="p in compParams" :key="p.key"
              :value="p.value" :min="p.min" :max="p.max" :origin="p.origin"
              :decimals="p.decimals" :unit="p.unit" :label="t(p.key)"
              :size="28" disabled
            />
          </div>
        </div>
      </div>
    </div>
  </section>
</template>

<script setup lang="ts">
import { computed, onBeforeUnmount, ref, watch } from 'vue';
import KnobField from './KnobField.vue';
import type { Bus, BusDsp, BusGate } from '~/types/project';
import { useMixerMeter } from '~/composables/useLiveMeters';

// Optional so the panel still renders before the first bus fetch lands.
const props = defineProps<{ bus?: Bus | null }>();

const emit = defineEmits<{
  (e: 'patch', id: string, patch: Partial<Bus>): void;
  (e: 'dsp-live', dsp: Partial<BusDsp>): void;
}>();

const { t } = useLocalization();
const server = useLiveplayServer();

// Whether the section is in circuit.
const dynIn = computed(() => props.bus?.dsp?.dynEnabled ?? true);

// Ranges are the conventional ones for each control. Times and the ratio are
// logarithmic: 1 ms to 2 ms is the same change of feel as 100 to 200, and a
// linear attack knob would spend nine tenths of its travel above 10 ms where
// almost nothing about a gate changes. Levels stay linear — decibels are
// already perceptually even.
//
// Hold starts at zero, where a logarithm has nowhere to go, so it is declared
// log and the knob falls back to linear on its own rather than being
// special-cased here.
type GateField = 'threshold' | 'ratio' | 'range' | 'attack' | 'hold' | 'release';
interface GateCtl {
  key: string; field: GateField; min: number; max: number; origin: number;
  decimals: number; unit: string; taper: 'linear' | 'log';
}
const gateParams: GateCtl[] = [
  { key: 'mixer.threshold', field: 'threshold', min: -80, max: 0,    origin: -40, decimals: 1, unit: 'dB', taper: 'linear' },
  { key: 'mixer.ratio',     field: 'ratio',     min: 1,   max: 20,   origin: 2,   decimals: 1, unit: ':1', taper: 'log' },
  { key: 'mixer.range',     field: 'range',     min: -80, max: 0,    origin: -20, decimals: 1, unit: 'dB', taper: 'linear' },
  { key: 'mixer.attack',    field: 'attack',    min: 0.1, max: 100,  origin: 1,   decimals: 1, unit: 'ms', taper: 'log' },
  { key: 'mixer.hold',      field: 'hold',      min: 0,   max: 1000, origin: 10,  decimals: 0, unit: 'ms', taper: 'log' },
  { key: 'mixer.release',   field: 'release',   min: 5,   max: 5000, origin: 100, decimals: 0, unit: 'ms', taper: 'log' },
];

const GATE_DEFAULTS: BusGate = {
  on: false, threshold: -40, ratio: 2, range: -20,
  attack: 1, hold: 10, release: 100,
};

// Same live-then-persist shape as everything else on this channel: the strip
// gets the value on every drag event over a call that writes no document, and
// the bus is written once the gesture settles.
const localGate = ref<BusGate | null>(null);
let   gateHold  = false;
let   gateSettle: ReturnType<typeof setTimeout> | null = null;

const gateValues = computed<BusGate>(() =>
  localGate.value ?? props.bus?.dsp?.gate ?? GATE_DEFAULTS);
const gateOn = computed(() => gateValues.value.on);

watch(() => props.bus?.dsp?.gate, () => { if (!gateHold) localGate.value = null; },
      { deep: true });
watch(() => props.bus?.id, () => { localGate.value = null; });
onBeforeUnmount(() => { if (gateSettle) clearTimeout(gateSettle); });

function pushGate(next: BusGate, persistNow: boolean) {
  localGate.value = next;
  gateHold = true;
  void server.setBusDsp(props.bus!.id, { gate: next }).catch(() => {});
  if (gateSettle) clearTimeout(gateSettle);
  if (persistNow) {
    // A switch is a discrete press, not a gesture: there is no later event to
    // re-arm a settle timer, so a settled write would never arrive.
    gateHold = false;
    emit('patch', props.bus!.id, { dsp: { gate: next } } as Partial<Bus>);
    return;
  }
  gateSettle = setTimeout(() => {
    gateSettle = null;
    gateHold   = false;
    emit('patch', props.bus!.id, { dsp: { gate: localGate.value ?? next } } as Partial<Bus>);
  }, 250);
}

function onGate(field: GateField, v: number) {
  if (!props.bus) return;
  pushGate({ ...gateValues.value, [field]: v }, false);
}

function toggleGate() {
  if (!props.bus) return;
  pushGate({ ...gateValues.value, on: !gateOn.value }, true);
}

// The section bypass. Takes out everything in the panel at once and gives it
// back untouched, which is the difference between it and switching each
// processor off individually.
function toggleSection() {
  if (!props.bus) return;
  const next = !dynIn.value;
  emit('dsp-live', { dynEnabled: next });
  void server.setBusDsp(props.bus.id, {
    // Carried alongside, because /dsp merges onto the STORED bus: a lone
    // dynEnabled would re-enable the section using whatever the document last
    // saved rather than what is on the surface right now.
    dynEnabled: next, gate: gateValues.value,
  }).catch(() => {});
  emit('patch', props.bus.id, { dsp: { dynEnabled: next } } as Partial<Bus>);
}

// ---- Gain reduction ------------------------------------------------------
// Reported per strip rather than per lane, because the gate's detector is
// linked across the lanes and so there is one figure for the channel.
const meter = useMixerMeter(() => props.bus?.mixerId);
const GR_FULL_DB = 40;   // the track's full height, in decibels of reduction
const gateGrPct = computed(() => {
  const gr = Math.min(0, meter.gateGr.value);
  return Math.min(100, (Math.abs(gr) / GR_FULL_DB) * 100);
});

const compParams = [
  { key: 'mixer.threshold', value: -18, min: -60, max: 0,    origin: -18, decimals: 1, unit: 'dB' },
  { key: 'mixer.ratio',     value: 4,   min: 1,   max: 60,   origin: 4,   decimals: 1, unit: ':1' },
  { key: 'mixer.makeup',    value: 0,   min: -12, max: 24,   origin: 0,   decimals: 1, unit: 'dB' },
  { key: 'mixer.attack',    value: 10,  min: 0.1, max: 300,  origin: 10,  decimals: 1, unit: 'ms' },
  { key: 'mixer.knee',      value: 6,   min: 0,   max: 24,   origin: 6,   decimals: 1, unit: 'dB' },
  { key: 'mixer.release',   value: 200, min: 5,   max: 5000, origin: 200, decimals: 0, unit: 'ms' },
];
</script>

<style scoped>
.dyn { min-height: 0; }
.dyn > .det__h { flex: 0 0 auto; }

.dyn__body {
  display: flex;
  gap: var(--spacing-sm);
  flex: 1 1 auto;
  min-height: 0;
}

/* Square, so the transfer curve keeps its 1:1 reading — a stretched dynamics
   graph lies about the slope. Capped, because square plus full panel height
   means it grows without limit on a tall window and swallows the panel; past
   this size it stops telling you anything more. */
/* Sized, not stretched: the box has no content of its own, so stretching was
   the only thing giving it height — and stretching is also what pinned it to
   the top of a panel taller than it. A definite height lets it centre against
   the controls beside it, which is where the eye expects the curve to sit. */
.dyn__graph {
  flex: 0 0 auto;
  align-self: center;
  aspect-ratio: 1;
  height: clamp(120px, 22vh, 170px);
  max-height: 100%;
  background: var(--color-background);
  border-radius: var(--border-radius-sm);
  overflow: hidden;
}
.dyn__svg { display: block; width: 100%; height: 100%; }
.dyn__grid  { stroke: var(--color-border); stroke-width: 1; opacity: 0.5; }
.dyn__unity { stroke: var(--color-accent); stroke-width: 2; }

/* Sized and centred to match the graph, so the two read as one block. */
.dyn__grmeters {
  display: flex;
  flex-direction: column;
  gap: var(--spacing-xs);
  flex: 0 0 auto;
  align-self: center;
  height: clamp(100px, 15vh, 170px);
  max-height: 100%;
  min-height: 0;
}
.dyn__gr {
  display: flex;
  align-items: center;
  gap: 3px;
  flex: 1 1 0;
  min-height: 0;
}
.dyn__grtrack {
  position: relative;
  width: 8px;
  height: 100%;
  min-height: 24px;
  background: var(--color-background);
  border: 1px solid var(--color-border);
  border-radius: 2px;
  overflow: hidden;
}
/* Grows DOWN from the top, because that is the direction gain reduction moves:
   the bar is how much is being taken away, not how much is getting through. */
.dyn__grfill {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  background: var(--color-accent);
  transition: height 60ms linear;
}
.dyn__grlabel {
  font-family: var(--font-mono);
  font-size: 8px;
  writing-mode: vertical-rl;
  color: var(--color-text-disabled);
}
/* The label doubles as the processor's in/out switch, so it has to read as
   pressable and to show which state it is in. */
.dyn__grlabel--btn {
  padding: 2px 0;
  background: none;
  border: none;
  cursor: pointer;
}
.dyn__grlabel--btn:hover:not(:disabled) { color: var(--color-text-primary); }
.dyn__grlabel--btn:disabled { cursor: not-allowed; opacity: 0.5; }
.dyn__grlabel--on { color: var(--color-accent); }

/* A processor that is switched out keeps its controls readable and adjustable
   — setting a gate up before switching it in is a normal way to work — but
   recedes so the panel says at a glance what is actually running. */
.dyn__group--out { opacity: 0.5; }
.dyn--bypassed .dyn__controls,
.dyn--bypassed .dyn__grmeters { opacity: 0.45; }
/* The compressor half is still a shell; the badge belongs to it alone. */
.dyn__group--pending .dyn__h { gap: var(--spacing-xs); }

/* The two groups sit centred in whatever height the panel has, packed to the
   left rather than stretched across it. */
.dyn__controls {
  display: flex;
  flex-direction: column;
  justify-content: center;
  gap: 8px;
  flex: 1 1 auto;
  min-width: 0;
}
.dyn__group { display: flex; flex-direction: column; gap: 1px; }
.dyn__h {
  margin: 0;
  font-size: 9px;
  letter-spacing: 0.06em;
  text-transform: uppercase;
  color: var(--color-text-disabled);
}
/* Six controls, always three across, so each processor reads as two tidy rows
   rather than reflowing into a ragged block as the panel resizes.
   Columns are sized to the knobs rather than to the panel: 1fr columns spread
   the six controls across whatever width was going, which left a group reading
   as scattered dots instead of a block you can take in at once. */
.dyn__row {
  display: grid;
  grid-template-columns: repeat(3, auto);
  justify-content: start;
  gap: 2px 10px;
}
</style>

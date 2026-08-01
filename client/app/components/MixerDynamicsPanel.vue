<template>
  <!--
    Dynamics: an expander/gate and a compressor/limiter, sharing one transfer
    graph.

    They share it because they act on the same axis — input level in, output
    level out — and a single curve is how you see what the two together
    actually do to a signal. Two graphs would show two halves of one answer.

    Shell until the DSP stage lands: dashed, labelled, controls disabled.
  -->
  <section class="dyn det__panel det__panel--pending">
    <h4 class="det__h">
      {{ t('mixer.tabDynamics') }}
      <span class="det__pending">{{ t('mixer.notImplemented') }}</span>
    </h4>

    <!-- Transfer curve: input level across, output level down. Unity is the
         diagonal; the gate pulls the bottom-left down and the compressor
         flattens the top-right. Drawn flat-unity until there is something to
         plot. -->
    <div class="dyn__graph">
      <svg viewBox="0 0 120 120" preserveAspectRatio="none" class="dyn__svg">
        <line v-for="g in [30, 60, 90]" :key="'v' + g" class="dyn__grid" :x1="g" :x2="g" y1="0" y2="120" />
        <line v-for="g in [30, 60, 90]" :key="'h' + g" class="dyn__grid" x1="0" x2="120" :y1="g" :y2="g" />
        <line class="dyn__unity" x1="0" y1="120" x2="120" y2="0" />
      </svg>
    </div>

    <div class="dyn__group">
      <h5 class="dyn__h">{{ t('mixer.gate') }}</h5>
      <div class="dyn__row">
        <KnobField
          v-for="p in gateParams" :key="p.key"
          :value="p.value" :min="p.min" :max="p.max" :origin="p.origin"
          :decimals="p.decimals" :unit="p.unit" :label="t(p.key)"
          :size="32" disabled
        />
      </div>
    </div>

    <div class="dyn__group">
      <h5 class="dyn__h">{{ t('mixer.compressor') }}</h5>
      <div class="dyn__row">
        <KnobField
          v-for="p in compParams" :key="p.key"
          :value="p.value" :min="p.min" :max="p.max" :origin="p.origin"
          :decimals="p.decimals" :unit="p.unit" :label="t(p.key)"
          :size="32" disabled
        />
      </div>
    </div>
  </section>
</template>

<script setup lang="ts">
import KnobField from './KnobField.vue';

const { t } = useLocalization();

// Ranges are the conventional ones for each control, so the knobs already
// travel correctly when the processors arrive and only the values need wiring.
const gateParams = [
  { key: 'mixer.threshold', value: -40, min: -80, max: 0,    origin: -40,  decimals: 1, unit: 'dB' },
  { key: 'mixer.ratio',     value: 2,   min: 1,   max: 20,   origin: 2,    decimals: 1, unit: ':1' },
  { key: 'mixer.range',     value: -20, min: -80, max: 0,    origin: -20,  decimals: 1, unit: 'dB' },
  { key: 'mixer.attack',    value: 1,   min: 0.1, max: 100,  origin: 1,    decimals: 1, unit: 'ms' },
  { key: 'mixer.hold',      value: 10,  min: 0,   max: 1000, origin: 10,   decimals: 0, unit: 'ms' },
  { key: 'mixer.release',   value: 100, min: 5,   max: 5000, origin: 100,  decimals: 0, unit: 'ms' },
];

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
.dyn { min-height: 0; overflow: auto; }

.dyn__graph {
  height: 96px;
  min-height: 64px;
  align-self: center;
  aspect-ratio: 1;
  background: var(--color-background);
  border-radius: var(--border-radius-sm);
  overflow: hidden;
}
.dyn__svg { display: block; width: 100%; height: 100%; }
.dyn__grid  { stroke: var(--color-border); stroke-width: 1; opacity: 0.5; }
.dyn__unity { stroke: var(--color-accent); stroke-width: 2; }

.dyn__group { display: flex; flex-direction: column; gap: 2px; }
.dyn__h {
  margin: 0;
  font-size: 9px;
  letter-spacing: 0.06em;
  text-transform: uppercase;
  color: var(--color-text-disabled);
}
.dyn__row {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(52px, 1fr));
  gap: 4px;
  justify-items: center;
}
</style>

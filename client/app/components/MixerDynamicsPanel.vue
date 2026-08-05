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
  <section class="dyn det__panel det__panel--pending">
    <h4 class="det__h">
      {{ t('mixer.tabDynamics') }}
      <span class="det__pending">{{ t('mixer.notImplemented') }}</span>
      <!-- The state is real and persists; the processing it will bypass does
           not exist yet, so the button is held disabled rather than offered as
           something that works. It goes live with the gate and compressor. -->
      <button
        class="det__byp"
        :class="{ 'det__byp--on': !dynIn }"
        disabled
        :title="t('mixer.bypassPending')"
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
        <div v-for="m in ['mixer.gateShort', 'mixer.compShort']" :key="m" class="dyn__gr">
          <div class="dyn__grtrack"></div>
          <span class="dyn__grlabel">{{ t(m) }}</span>
        </div>
      </div>

      <div class="dyn__controls">
        <div class="dyn__group">
          <h5 class="dyn__h">{{ t('mixer.gate') }}</h5>
          <div class="dyn__row">
            <KnobField
              v-for="p in gateParams" :key="p.key"
              :value="p.value" :min="p.min" :max="p.max" :origin="p.origin"
              :decimals="p.decimals" :unit="p.unit" :label="t(p.key)"
              :size="28" disabled
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
              :size="28" disabled
            />
          </div>
        </div>
      </div>
    </div>
  </section>
</template>

<script setup lang="ts">
import { computed } from 'vue';
import KnobField from './KnobField.vue';
import type { Bus } from '~/types/project';

// Optional so the panel still renders before the first bus fetch lands.
const props = defineProps<{ bus?: Bus | null }>();

const { t } = useLocalization();

// Whether the section is in circuit. Read-only for now: the state persists on
// the bus and the button is disabled, because there is no dynamics processing
// for it to take out yet.
const dynIn = computed(() => props.bus?.dsp?.dynEnabled ?? true);

// Ranges are the conventional ones for each control, so the knobs already
// travel correctly when the processors arrive and only the values need wiring.
// Six each, which lays out as two rows of three.
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
  width: 8px;
  height: 100%;
  min-height: 24px;
  background: var(--color-background);
  border: 1px solid var(--color-border);
  border-radius: 2px;
}
.dyn__grlabel {
  font-family: var(--font-mono);
  font-size: 8px;
  writing-mode: vertical-rl;
  color: var(--color-text-disabled);
}

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

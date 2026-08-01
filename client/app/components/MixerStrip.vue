<template>
  <!--
    One bus, as a channel strip. Ordered the way a desk is, top to bottom:
    inserts, output assignment, pan, mute/PFL, then fader and meter taking the
    dominant vertical space, with the name at the bottom where the scribble
    strip lives.
  -->
  <div
    class="strip"
    :class="{ 'strip--selected': selected, 'strip--muted': bus.mute, 'strip--touch': touch }"
    @click="$emit('select', bus.id)"
  >
    <!-- Inserts: scaffolding until the DSP chain exists. -->
    <div class="strip__inserts">
      <button
        v-for="n in 2"
        :key="n"
        class="strip__insert"
        :title="t('mixer.insertsComingSoon')"
        disabled
      >—</button>
    </div>

    <!-- Output assignment: the most consequential control on the strip. -->
    <div class="strip__row">
      <select
        class="strip__output"
        :value="outputValue"
        :title="outputTitle"
        :class="{ 'strip__output--warn': outputUnmapped }"
        @click.stop
        @change="onOutputChange"
      >
        <option value="master">{{ t('mixer.toMaster') }}</option>
        <option
          v-for="o in outputNames"
          :key="'out:' + o"
          :value="'out:' + o"
        >{{ o }}</option>
      </select>
    </div>

    <div class="strip__row strip__row--split">
      <button
        class="strip__width"
        :title="t('mixer.widthToggle')"
        :disabled="bus.system"
        @click.stop="$emit('patch', bus.id, { width: bus.width >= 2 ? 1 : 2 })"
      >{{ bus.width >= 2 ? 'ST' : 'M' }}</button>
      <button class="strip__fx" :title="t('mixer.channelDetails')" @click.stop="$emit('open', bus.id)">
        <span class="material-symbols-rounded">tune</span>
      </button>
    </div>

    <!-- Mute / PFL -->
    <div class="strip__row strip__row--split">
      <button
        class="strip__btn"
        :class="{ 'strip__btn--mute': bus.mute }"
        @click.stop="$emit('patch', bus.id, { mute: !bus.mute })"
      >{{ t('mixer.mute') }}</button>
      <button
        class="strip__btn"
        :class="{ 'strip__btn--pfl': pfl }"
        :title="t('mixer.pflComingSoon')"
        disabled
      >{{ t('mixer.pfl') }}</button>
    </div>

    <!-- Meter + fader, sharing the dominant vertical space. -->
    <div class="strip__meterfader">
      <LiveMeterBar
        v-if="bus.mixerId"
        source="mixer"
        :mixer-id="bus.mixerId"
        vertical
        :min-db="-60"
        :max-db="0"
      />
      <div v-else class="strip__nometer" :title="t('mixer.noStrip')"></div>
      <CanvasFader
        :db="bus.gainDb"
        :min-db="-60"
        :max-db="6"
        :width="touch ? 32 : 20"
        @input="(db: number) => $emit('patch', bus.id, { gainDb: db })"
        @reset="$emit('patch', bus.id, { gainDb: 0 })"
      />
    </div>

    <div class="strip__gain">{{ gainLabel }}</div>

    <!-- Scribble strip. -->
    <div class="strip__name" :title="bus.name">
      <span class="strip__chip" :style="{ background: bus.color || 'var(--color-accent)' }"></span>
      <span class="strip__nametext">{{ bus.name }}</span>
    </div>
    <div class="strip__count">{{ t('mixer.itemCount', { count: bus.itemUuids.length }) }}</div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue';
import type { Bus } from '~/types/project';
import CanvasFader from './CanvasFader.vue';
import LiveMeterBar from './LiveMeterBar.vue';

const props = defineProps<{
  bus: Bus;
  selected?: boolean;
  touch?: boolean;
  /** Logical output names this machine knows about. */
  outputNames: string[];
  /** PFL is engine work that hasn't landed; the control is shown disabled. */
  pfl?: boolean;
}>();

const emit = defineEmits<{
  (e: 'select', id: string): void;
  (e: 'open', id: string): void;
  (e: 'patch', id: string, patch: Partial<Bus>): void;
}>();

const { t } = useLocalization();

const gainLabel = computed(() => {
  const v = props.bus.gainDb;
  if (v <= -60) return '-∞';
  return (v > 0 ? '+' : '') + v.toFixed(1);
});

// The <select> carries "master" or "out:<logical name>". Bus→bus targets are
// shown as-is but not selectable yet — that routing isn't implemented.
const outputValue = computed(() => {
  const o = props.bus.output;
  return o.type === 'output' ? 'out:' + o.target : 'master';
});

// A bus pointing at a name this machine has no mapping for still plays (the
// name is treated as a device) but should look different from a healthy one.
const outputUnmapped = computed(() =>
  props.bus.output.type === 'output' && !props.outputNames.includes(props.bus.output.target));

const outputTitle = computed(() => {
  if (props.bus.output.type === 'bus') return t('mixer.busToBusUnsupported');
  if (outputUnmapped.value) return t('mixer.outputUnmapped', { name: props.bus.output.target });
  return t('mixer.output');
});

function onOutputChange(e: Event) {
  const v = (e.target as HTMLSelectElement).value;
  emit('patch', props.bus.id, v === 'master'
    ? { output: { type: 'master', target: '' } }
    : { output: { type: 'output', target: v.slice(4) } });
}
</script>

<style scoped>
.strip {
  display: flex;
  flex-direction: column;
  gap: var(--spacing-xs);
  width: 76px;
  flex: 0 0 auto;
  padding: var(--spacing-xs);
  background: var(--color-surface);
  border: 1px solid var(--color-border);
  border-radius: var(--border-radius-md);
  cursor: pointer;
  transition: border-color var(--transition-fast);
}
.strip--touch { width: 118px; }
.strip:hover { border-color: var(--color-text-disabled); }
.strip--selected { border-color: var(--color-accent); }
.strip--muted .strip__meterfader { opacity: 0.45; }

.strip__inserts { display: flex; flex-direction: column; gap: 2px; }
.strip__insert {
  height: 16px;
  font-size: 10px;
  color: var(--color-text-disabled);
  background: var(--color-background);
  border: 1px solid var(--color-border);
  border-radius: var(--border-radius-sm);
  cursor: not-allowed;
}

.strip__row { display: flex; gap: 2px; }
.strip__row--split > * { flex: 1; }

.strip__output {
  width: 100%;
  font-size: 10px;
  padding: 2px;
  color: var(--color-text-primary);
  background: var(--color-background);
  border: 1px solid var(--color-border);
  border-radius: var(--border-radius-sm);
}
.strip__output--warn { border-color: var(--color-warning); }

.strip__width,
.strip__fx,
.strip__btn {
  font-size: 10px;
  padding: 3px 0;
  color: var(--color-text-secondary);
  background: var(--color-background);
  border: 1px solid var(--color-border);
  border-radius: var(--border-radius-sm);
  cursor: pointer;
}
.strip__fx { display: flex; align-items: center; justify-content: center; }
.strip__fx .material-symbols-rounded { font-size: 14px; }
.strip__btn--mute {
  background: var(--color-danger);
  border-color: var(--color-danger);
  color: #fff;
}
.strip__btn--pfl { background: var(--color-success); border-color: var(--color-success); color: #fff; }
.strip__btn:disabled, .strip__width:disabled { cursor: not-allowed; opacity: 0.4; }

.strip__meterfader {
  display: flex;
  gap: var(--spacing-xs);
  justify-content: center;
  flex: 1;
  min-height: 140px;
}
.strip__nometer { width: 6px; background: var(--color-background); border-radius: 2px; }

.strip__gain {
  text-align: center;
  font-family: var(--font-mono);
  font-size: 11px;
  color: var(--color-text-primary);
}

.strip__name {
  display: flex;
  align-items: center;
  gap: 4px;
  padding: 3px;
  background: var(--color-background);
  border-radius: var(--border-radius-sm);
  overflow: hidden;
}
.strip__chip { width: 6px; height: 6px; border-radius: 50%; flex: 0 0 auto; }
.strip__nametext {
  font-size: 11px;
  color: var(--color-text-primary);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.strip__count {
  text-align: center;
  font-size: 9px;
  color: var(--color-text-disabled);
}
</style>

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
      <button class="strip__fx" :title="t('mixer.channelDetails')" @click.stop="$emit('open', bus.id)"><!-- opens details, forcing full width: the three-column layout is unusable in a docked pane -->
        <span class="material-symbols-rounded">tune</span>
      </button>
    </div>

    <!-- Pan. Only a mono bus has one: pan is the position of a single lane
         between the two lanes of a stereo destination, which is what a mono
         channel's pot does. A stereo bus would want balance, which isn't
         built — the knob stays visible but disabled so strips keep the same
         height and their meters stay aligned across the rail. -->
    <div class="strip__pan" @click.stop>
      <Knob
        :value="pan"
        :min="-1"
        :max="1"
        :origin="0"
        :size="touch ? 40 : 30"
        :disabled="bus.width >= 2"
        :title="bus.width >= 2 ? t('mixer.balanceUnsupported') : t('mixer.pan')"
        @input="onPan"
        @reset="onPan(0)"
      />
      <span class="strip__panlabel">{{ panLabel }}</span>
    </div>

    <!-- Mute / PFL -->
    <div class="strip__row strip__row--split">
      <button
        class="strip__btn"
        :class="{ 'strip__btn--mute': bus.mute }"
        @click.stop="onMute"
      >{{ t('mixer.mute') }}</button>
      <button
        class="strip__btn"
        :class="{ 'strip__btn--pfl': pfl }"
        :title="t('mixer.pflComingSoon')"
        disabled
      >{{ t('mixer.pfl') }}</button>
    </div>

    <!-- Meter, shared scale, fader.
         The scale and fader span the full -60..+12. The meter is dBFS and
         tops out at 0, so its track is only as tall as that part of the range
         — 0 dBFS lands exactly on the 0 tick, the meter keeps full resolution
         across its own range, and there is no dead strip above it that a
         signal could never reach. One scale stays honest for both because
         both map dB to position linearly. -->
    <div class="strip__meterfader">
      <div class="strip__meters" :style="{ height: METER_TRACK_PCT + '%' }">
        <template v-if="bus.mixerId">
          <LiveMeterBar
            v-for="lane in bus.width >= 2 ? [0, 1] : [null]"
            :key="'lane' + lane"
            source="mixer"
            :mixer-id="bus.mixerId"
            :lane="lane"
            vertical
            :min-db="FADER_MIN_DB"
            :max-db="METER_MAX_DB"
          />
        </template>
        <div v-else class="strip__nometer" :title="t('mixer.noStrip')"></div>
      </div>
      <MeterScale :min-db="FADER_MIN_DB" :max-db="FADER_MAX_DB" />
      <CanvasFader
        :db="gainDb"
        :min-db="FADER_MIN_DB"
        :max-db="FADER_MAX_DB"
        :width="touch ? 32 : 20"
        @input="onFader"
        @reset="onFader(0)"
      />
    </div>

    <div class="strip__gain">{{ gainLabel }}</div>

    <!-- Scribble strip. Double-click to rename, as on a desk. -->
    <div class="strip__name" :title="renaming ? '' : bus.name + ' — ' + t('mixer.renameHint')">
      <span class="strip__chip" :style="{ background: bus.color || 'var(--color-accent)' }"></span>
      <input
        v-if="renaming"
        ref="nameInput"
        class="strip__nameinput"
        :value="bus.name"
        @click.stop
        @keyup.enter="commitRename"
        @keyup.esc="renaming = false"
        @blur="commitRename"
      />
      <span v-else class="strip__nametext" @dblclick.stop="startRename">{{ bus.name }}</span>
    </div>
    <div class="strip__count">{{ t('mixer.itemCount', { count: bus.itemUuids.length }) }}</div>
  </div>
</template>

<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, ref, watch } from 'vue';
import type { Bus } from '~/types/project';
import CanvasFader from './CanvasFader.vue';
import Knob from './Knob.vue';
import LiveMeterBar from './LiveMeterBar.vue';
import MeterScale from './MeterScale.vue';
import {
  FADER_MIN_DB, FADER_MAX_DB, METER_MAX_DB, METER_TRACK_PCT,
} from '~/utils/meterScale';

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
const server = useLiveplayServer();

// The fader is driven locally while it moves. Binding it straight to
// bus.gainDb meant every drag event did a PATCH plus a full bus refetch, and
// the knob snapped back to the stale value until the round-trip landed —
// unusable, and brutal on the server.
//
// So: the engine gets the level immediately (a strip-only call that writes no
// document and triggers no refetch) and the bus is persisted once the gesture
// settles.
const gainDb  = ref(props.bus.gainDb);
let   holding = false;
let   settle: ReturnType<typeof setTimeout> | null = null;

watch(() => props.bus.gainDb, v => { if (!holding) gainDb.value = v; });

function onFader(db: number) {
  gainDb.value = db;
  holding = true;
  if (props.bus.mixerId) void server.setMixerGainDb(props.bus.mixerId, db).catch(() => {});
  if (settle) clearTimeout(settle);
  settle = setTimeout(() => {
    settle  = null;
    holding = false;
    emit('patch', props.bus.id, { gainDb: gainDb.value });
  }, 250);
}

// Pan follows the fader's pattern for the same reason: the engine gets the
// position immediately over a strip-only call, and the bus is persisted once
// the gesture settles. Unlike gain, pan lives in the bus's send gains rather
// than on the strip, so the live call is /api/buses/<id>/pan.
const pan       = ref(props.bus.pan ?? 0);
let   panHold   = false;
let   panSettle: ReturnType<typeof setTimeout> | null = null;

watch(() => props.bus.pan, v => { if (!panHold) pan.value = v ?? 0; });

function onPan(v: number) {
  if (props.bus.width >= 2) return;
  pan.value = v;
  panHold = true;
  void server.setBusPan(props.bus.id, v).catch(() => {});
  if (panSettle) clearTimeout(panSettle);
  panSettle = setTimeout(() => {
    panSettle = null;
    panHold   = false;
    emit('patch', props.bus.id, { pan: pan.value });
  }, 250);
}

// L/R offset in the usual console notation: C at centre, L50/R50 halfway.
const panLabel = computed(() => {
  if (props.bus.width >= 2) return '--';
  const v = Math.round(pan.value * 100);
  if (v === 0) return 'C';
  return (v < 0 ? 'L' : 'R') + Math.abs(v);
});

onBeforeUnmount(() => {
  if (settle) clearTimeout(settle);
  if (panSettle) clearTimeout(panSettle);
});

const renaming  = ref(false);
const nameInput = ref<HTMLInputElement | null>(null);

async function startRename() {
  if (props.bus.system) return;
  renaming.value = true;
  await nextTick();
  nameInput.value?.select();
}
function commitRename() {
  if (!renaming.value) return;
  renaming.value = false;
  const next = nameInput.value?.value?.trim();
  if (next && next !== props.bus.name) emit('patch', props.bus.id, { name: next });
}

// Mute goes to the engine first so it takes effect on the click rather than
// after the document round-trip, then persists.
function onMute() {
  const next = !props.bus.mute;
  if (props.bus.mixerId) void server.setMixerMute(props.bus.mixerId, next).catch(() => {});
  emit('patch', props.bus.id, { mute: next });
}

const gainLabel = computed(() => {
  const v = gainDb.value;
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
  /* Wider than before: two lane meters plus the shared scale plus the fader. */
  width: 88px;
  flex: 0 0 auto;
  padding: var(--spacing-xs);
  background: var(--color-surface);
  border: 1px solid var(--color-border);
  border-radius: var(--border-radius-md);
  cursor: pointer;
  transition: border-color var(--transition-fast);
}
.strip--touch { width: 132px; }
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

.strip__pan {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 4px;
}
.strip__panlabel {
  font-family: var(--font-mono);
  font-size: 10px;
  min-width: 26px;
  color: var(--color-text-secondary);
}

.strip__meterfader {
  display: flex;
  gap: 3px;
  justify-content: center;
  /* Default stretch: the scale and fader must span the full range. Only the
     meter block is shorter, and it aligns to the bottom (below) so its
     0 dBFS top edge lands on the scale's 0 tick. */
  flex: 1;
  min-height: 150px;
}
.strip__meters {
  display: flex;
  gap: 2px;
  align-self: flex-end;
}
.strip__nometer { width: 6px; height: 100%; background: var(--color-background); border-radius: 2px; }

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
  cursor: text;
}
.strip__nameinput {
  width: 100%;
  min-width: 0;
  font-size: 11px;
  color: var(--color-text-primary);
  background: var(--color-surface);
  border: 1px solid var(--color-accent);
  border-radius: 2px;
  padding: 0 2px;
}
.strip__count {
  text-align: center;
  font-size: 9px;
  color: var(--color-text-disabled);
}
</style>

<template>
  <!--
    The channel view: one bus, opened out across the whole mixer window.

    This is the layout a large-format live console uses for its channel screen,
    and the reason to follow it is that the shape carries meaning. The selected
    channel's strip stays on the left, unchanged and full height, so the fader
    you were just holding is still under your hand and still reads the same.
    Everything that processes that channel occupies the rest of the window, all
    of it visible at once — an operator reaching for a compressor mid-show
    should not have to find a tab first. Channel selection lives along the
    bottom, where a console puts its select row, with arrows for stepping.

    EQ, Dynamics and Inserts are labelled shells until the DSP stage lands.
    They are laid out at full size rather than hidden behind tabs so that stage
    fills panels instead of inventing navigation late.
  -->
  <div class="det">
    <header class="det__head">
      <span class="det__chip" :style="{ background: bus.color || 'var(--color-accent)' }"></span>
      <input
        class="det__name"
        :value="bus.name"
        :disabled="bus.system"
        @change="$emit('patch', bus.id, { name: ($event.target as HTMLInputElement).value })"
      />
      <span class="det__meta">{{ widthLabel }} · {{ outputSummary }}</span>

      <div class="det__spacer"></div>
      <button
        v-if="!bus.system"
        class="det__delete"
        :title="t('mixer.deleteBus')"
        @click="$emit('delete', bus.id)"
      >
        <span class="material-symbols-rounded">delete</span>
      </button>
      <button class="det__close" :title="t('mixer.backToMixer')" @click="$emit('close')">
        <span class="material-symbols-rounded">close</span>
      </button>
    </header>

    <div class="det__main">
      <!-- The channel itself: the same strip as the rail, full height. -->
      <aside class="det__strip">
        <!-- The strip's channel-view button is already satisfied here, so it
             toggles: pressing it again goes back to the rail. -->
        <MixerStrip
          :bus="bus"
          :output-names="outputNames"
          :touch="touch"
          selected
          @patch="(id: string, p: Partial<Bus>) => $emit('patch', id, p)"
          @open="$emit('close')"
        />
      </aside>

      <!-- Everything that processes it, all visible at once. -->
      <div class="det__proc">
        <section class="det__panel">
          <h4 class="det__h">{{ t('mixer.output') }}</h4>
          <label class="det__field">
            <span>{{ t('mixer.output') }}</span>
            <select :value="outputValue" @change="onOutputChange">
              <option value="master">{{ t('mixer.toMaster') }}</option>
              <option v-for="o in outputNames" :key="'out:' + o" :value="'out:' + o">{{ o }}</option>
            </select>
          </label>
          <label class="det__field">
            <span>{{ t('mixer.width') }}</span>
            <select
              :value="bus.width"
              :disabled="bus.system"
              @change="$emit('patch', bus.id, { width: Number(($event.target as HTMLSelectElement).value) })"
            >
              <option :value="1">{{ t('mixer.mono') }}</option>
              <option :value="2">{{ t('mixer.stereo') }}</option>
            </select>
          </label>
          <p v-if="unmapped" class="det__warn">
            {{ t('mixer.outputUnmapped', { name: bus.output.target }) }}
          </p>
          <p v-if="bus.output.type === 'bus'" class="det__warn">
            {{ t('mixer.busToBusUnsupported') }}
          </p>
        </section>

        <section class="det__panel det__panel--pending">
          <h4 class="det__h">
            {{ t('mixer.tabEq') }}
            <span class="det__pending">{{ t('mixer.notImplemented') }}</span>
          </h4>
          <div class="det__curve">
            <span class="material-symbols-rounded">equalizer</span>
          </div>
          <div class="det__bands">
            <div v-for="band in eqBands" :key="band" class="det__band">
              <span class="det__bandname">{{ band }}</span>
              <Knob v-for="p in eqParams" :key="p" :value="0" :min="-1" :max="1" :size="30" disabled />
            </div>
          </div>
        </section>

        <section class="det__panel det__panel--pending">
          <h4 class="det__h">
            {{ t('mixer.tabDynamics') }}
            <span class="det__pending">{{ t('mixer.notImplemented') }}</span>
          </h4>
          <div class="det__knobrow">
            <div v-for="p in dynParams" :key="p" class="det__param">
              <Knob :value="0" :min="-1" :max="1" :size="38" disabled />
              <span class="det__paramname">{{ t(p) }}</span>
            </div>
          </div>
        </section>

        <section class="det__panel det__panel--pending">
          <h4 class="det__h">
            {{ t('mixer.tabInserts') }}
            <span class="det__pending">{{ t('mixer.notImplemented') }}</span>
          </h4>
          <button v-for="n in 4" :key="n" class="det__insert" disabled>
            <span class="det__insertnum">{{ n }}</span>
            <span>{{ t('mixer.emptySlot') }}</span>
          </button>
        </section>

        <section class="det__panel">
          <h4 class="det__h">{{ t('mixer.feedingThis') }}</h4>
          <ul class="det__items">
            <li v-for="uuid in bus.itemUuids" :key="uuid">{{ itemName(uuid) }}</li>
            <li v-if="bus.itemUuids.length === 0" class="det__none">{{ t('mixer.nothingAssigned') }}</li>
          </ul>
        </section>
      </div>
    </div>

    <!-- Channel select row, where a console puts it. -->
    <footer class="det__bank">
      <button
        class="det__nav"
        :disabled="!prevId"
        :title="t('mixer.prevChannel')"
        @click="$emit('select', prevId)"
      >
        <span class="material-symbols-rounded">chevron_left</span>
      </button>

      <div class="det__banklist">
        <button
          v-for="b in buses"
          :key="b.id"
          class="det__mini"
          :class="{ 'det__mini--active': b.id === bus.id }"
          @click="$emit('select', b.id)"
        >
          <span class="det__minichip" :style="{ background: b.color || 'var(--color-accent)' }"></span>
          <span class="det__mininame">{{ b.name }}</span>
        </button>
      </div>

      <button
        class="det__nav"
        :disabled="!nextId"
        :title="t('mixer.nextChannel')"
        @click="$emit('select', nextId)"
      >
        <span class="material-symbols-rounded">chevron_right</span>
      </button>
    </footer>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue';
import type { Bus } from '~/types/project';
import MixerStrip from './MixerStrip.vue';
import Knob from './Knob.vue';

const props = defineProps<{
  bus: Bus;
  buses: Bus[];
  outputNames: string[];
}>();

const emit = defineEmits<{
  (e: 'patch', id: string, patch: Partial<Bus>): void;
  (e: 'delete', id: string): void;
  (e: 'select', id: string): void;
  (e: 'close'): void;
}>();

const { t } = useLocalization();
const { findItemByUuid } = useProject();
const { uiMode } = useUiMode();
const touch = computed(() => uiMode.value === 'playback');

// Shells for the DSP stage. Named here rather than in the template so the
// panels read as a list of parameters a processor will have, not as decoration.
const eqBands  = ['LF', 'LMF', 'HMF', 'HF'];
const eqParams = ['freq', 'gain', 'q'];
const dynParams = [
  'mixer.threshold', 'mixer.ratio', 'mixer.attack', 'mixer.release',
];

// Stepping order follows the rail, and includes buses the engine currently has
// no strip for — they are still channels, and skipping them would make the
// arrows land somewhere other than the next tile in the row below.
const index  = computed(() => props.buses.findIndex(b => b.id === props.bus.id));
const prevId = computed(() => props.buses[index.value - 1]?.id ?? '');
const nextId = computed(() => props.buses[index.value + 1]?.id ?? '');

const widthLabel = computed(() =>
  props.bus.width >= 2 ? t('mixer.stereo') : t('mixer.mono'));

const outputValue = computed(() =>
  props.bus.output.type === 'output' ? 'out:' + props.bus.output.target : 'master');

const outputSummary = computed(() =>
  props.bus.output.type === 'output' ? props.bus.output.target : t('mixer.toMaster'));

const unmapped = computed(() =>
  props.bus.output.type === 'output' && !props.outputNames.includes(props.bus.output.target));

function onOutputChange(e: Event) {
  const v = (e.target as HTMLSelectElement).value;
  emit('patch', props.bus.id, v === 'master'
    ? { output: { type: 'master', target: '' } }
    : { output: { type: 'output', target: v.slice(4) } });
}

function itemName(uuid: string): string {
  const it = findItemByUuid?.(uuid) as any;
  return it?.displayName || uuid;
}
</script>

<style scoped>
.det {
  display: flex;
  flex-direction: column;
  flex: 1;
  min-height: 0;
  min-width: 0;
  background: var(--color-background);
}

.det__head {
  display: flex;
  align-items: center;
  gap: var(--spacing-xs);
  flex: 0 0 auto;
  padding: var(--spacing-xs) var(--spacing-sm);
  border-bottom: 1px solid var(--color-border);
}
.det__chip { width: 10px; height: 10px; border-radius: 50%; flex: 0 0 auto; }
.det__name {
  width: 180px;
  font-size: 13px;
  padding: 3px 6px;
  color: var(--color-text-primary);
  background: var(--color-surface);
  border: 1px solid var(--color-border);
  border-radius: var(--border-radius-sm);
}
.det__meta { font-size: 11px; color: var(--color-text-disabled); }
.det__delete, .det__close {
  display: flex;
  color: var(--color-text-secondary);
  background: none;
  border: none;
  cursor: pointer;
}
.det__delete:hover, .det__close:hover { color: var(--color-text-primary); }
.det__spacer { flex: 1; }

/* Strip left, processing right — the strip keeps its own width so it is the
   same object it was in the rail, not a smaller redrawing of it. */
.det__main { display: flex; flex: 1; min-height: 0; min-width: 0; }
.det__strip {
  display: flex;
  flex: 0 0 auto;
  min-height: 0;
  padding: var(--spacing-sm);
  border-right: 1px solid var(--color-border);
  background: var(--color-surface);
}

.det__proc {
  display: grid;
  /* Panels flow into as many columns as the window allows and reflow when it
     narrows, so the same view works docked-wide and on a laptop screen. */
  grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
  align-content: start;
  gap: var(--spacing-sm);
  flex: 1;
  min-width: 0;
  padding: var(--spacing-sm);
  overflow-y: auto;
}

.det__panel {
  display: flex;
  flex-direction: column;
  gap: var(--spacing-xs);
  padding: var(--spacing-sm);
  background: var(--color-surface);
  border: 1px solid var(--color-border);
  border-radius: var(--border-radius-md);
}
/* Shells read as unfinished on purpose — a dashed edge and a label, so nothing
   here can be mistaken for a control that does something. */
.det__panel--pending { border-style: dashed; }
.det__panel--pending .det__curve,
.det__panel--pending .det__band,
.det__panel--pending .det__knobrow { opacity: 0.5; }

.det__h {
  display: flex;
  align-items: center;
  gap: var(--spacing-xs);
  margin: 0;
  font-size: 10px;
  letter-spacing: 0.08em;
  text-transform: uppercase;
  color: var(--color-text-secondary);
}
.det__pending {
  font-size: 9px;
  letter-spacing: 0.04em;
  text-transform: none;
  color: var(--color-text-disabled);
}

.det__curve {
  display: flex;
  align-items: center;
  justify-content: center;
  height: 88px;
  color: var(--color-text-disabled);
  background: var(--color-background);
  border-radius: var(--border-radius-sm);
}
.det__curve .material-symbols-rounded { font-size: 32px; }

.det__bands { display: flex; flex-direction: column; gap: 4px; }
.det__band { display: flex; align-items: center; gap: 6px; }
.det__bandname {
  width: 34px;
  font-family: var(--font-mono);
  font-size: 10px;
  color: var(--color-text-secondary);
}

.det__knobrow { display: flex; flex-wrap: wrap; gap: var(--spacing-sm); }
.det__param { display: flex; flex-direction: column; align-items: center; gap: 2px; }
.det__paramname { font-size: 9px; color: var(--color-text-secondary); }

.det__insert {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 11px;
  padding: 5px 6px;
  color: var(--color-text-disabled);
  background: var(--color-background);
  border: 1px solid var(--color-border);
  border-radius: var(--border-radius-sm);
  cursor: not-allowed;
}
.det__insertnum {
  font-family: var(--font-mono);
  font-size: 9px;
  opacity: 0.6;
}

.det__field {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 6px;
  font-size: 11px;
  color: var(--color-text-secondary);
}
.det__field select {
  font-size: 11px;
  padding: 3px;
  color: var(--color-text-primary);
  background: var(--color-background);
  border: 1px solid var(--color-border);
  border-radius: var(--border-radius-sm);
}

.det__warn { margin: 0; font-size: 11px; color: var(--color-warning); }
.det__items { margin: 0; padding-left: 16px; font-size: 11px; color: var(--color-text-primary); }
.det__none { list-style: none; margin-left: -16px; color: var(--color-text-disabled); }

.det__bank {
  display: flex;
  align-items: center;
  gap: var(--spacing-xs);
  flex: 0 0 auto;
  padding: var(--spacing-xs) var(--spacing-sm);
  border-top: 1px solid var(--color-border);
  background: var(--color-surface);
}
.det__banklist { display: flex; gap: 2px; flex: 1; min-width: 0; overflow-x: auto; }
.det__nav {
  display: flex;
  flex: 0 0 auto;
  padding: 4px;
  color: var(--color-text-secondary);
  background: var(--color-background);
  border: 1px solid var(--color-border);
  border-radius: var(--border-radius-sm);
  cursor: pointer;
}
.det__nav:disabled { opacity: 0.3; cursor: default; }
.det__mini {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 10px;
  padding: 4px 8px;
  color: var(--color-text-secondary);
  background: var(--color-background);
  border: 1px solid var(--color-border);
  border-radius: var(--border-radius-sm);
  cursor: pointer;
  white-space: nowrap;
}
.det__mini--active {
  border-color: var(--color-accent);
  color: var(--color-text-primary);
}
.det__minichip { width: 6px; height: 6px; border-radius: 50%; flex: 0 0 auto; }
</style>

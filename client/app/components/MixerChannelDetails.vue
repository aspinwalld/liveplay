<template>
  <!--
    The channel view: one bus, opened out across the whole mixer window.

    This is the layout a large-format live console uses for its channel screen,
    and the shape carries meaning. The channel column stands on the left at full
    height, with its own fader under your hand. Processing occupies the rest,
    all of it visible at once — an operator reaching for a compressor mid-show
    should not have to find a tab first. Channel selection runs along the
    bottom, where a console puts its select row, with arrows for stepping.

    Reading order across the work area: what shapes the sound (EQ, then
    dynamics beneath it), then what is inserted into it (plugins), then what is
    connected to it in either direction (contributions in, sends out).

    EQ, dynamics, filters and plugins are labelled shells until the DSP stage
    lands. They are laid out at full size rather than hidden so that stage fills
    panels instead of inventing navigation late.
  -->
  <div class="det">
    <header class="det__head">
      <span class="det__chip" :style="{ background: bus.color || 'var(--color-accent)' }"></span>
      <span class="det__title">{{ bus.name }}</span>
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
      <MixerChannelFader
        :bus="bus"
        :prev-id="prevId"
        :next-id="nextId"
        @patch="(id: string, p: Partial<Bus>) => $emit('patch', id, p)"
        @select="(id: string) => $emit('select', id)"
      />

      <div class="det__work">
        <!-- EQ and dynamics share the left column of the work area, half the
             height each where there is room. Below the breakpoint the whole
             area becomes one column and they take the height they need, with
             the area scrolling. -->
        <MixerEqPanel class="det__eq" />
        <MixerDynamicsPanel class="det__dyn" />

        <section class="det__panel det__panel--pending det__plugins">
          <h4 class="det__h">
            {{ t('mixer.plugins') }}
            <span class="det__pending">{{ t('mixer.notImplemented') }}</span>
          </h4>
          <button v-for="n in 6" :key="n" class="det__insert" disabled>
            <span class="det__insertnum">{{ n }}</span>
            <span>{{ t('mixer.emptySlot') }}</span>
          </button>
        </section>

        <div class="det__io">
          <section class="det__panel">
            <h4 class="det__h">{{ t('mixer.feedingThis') }}</h4>
            <ul class="det__items">
              <li v-for="uuid in bus.itemUuids" :key="uuid">{{ itemName(uuid) }}</li>
              <li v-if="bus.itemUuids.length === 0" class="det__none">{{ t('mixer.nothingAssigned') }}</li>
            </ul>
          </section>

          <section class="det__panel">
            <h4 class="det__h">{{ t('mixer.sends') }}</h4>
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
            <p class="det__none">{{ t('mixer.auxSendsPending') }}</p>
          </section>
        </div>
      </div>
    </div>

    <!-- Channel select row, where a console puts it. Each tile carries a live
         meter: the point of the row is knowing which channel to go to, and on
         a desk that judgement is made by watching level, not by reading names.
         No scale — at this size it would be unreadable — but the same meter
         component as everywhere else, so the colours mean the same thing. -->
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
          <span class="det__minimeter">
            <StereoMeter
              v-if="b.mixerId"
              :mixer-id="b.mixerId"
              :mono="b.width < 2"
              bare
              :show-scale="false"
              :min-db="FADER_MIN_DB"
              :max-db="METER_MAX_DB"
            />
          </span>
          <span class="det__minitext">
            <span class="det__minichip" :style="{ background: b.color || 'var(--color-accent)' }"></span>
            <span class="det__mininame">{{ b.name }}</span>
          </span>
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
import MixerChannelFader from './MixerChannelFader.vue';
import MixerEqPanel from './MixerEqPanel.vue';
import MixerDynamicsPanel from './MixerDynamicsPanel.vue';
import StereoMeter from './StereoMeter.vue';
import { FADER_MIN_DB, METER_MAX_DB } from '~/utils/meterScale';

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
/* The name is editable in the channel column, where it sits between the
   stepping arrows; here it is just the heading. */
.det__title { font-size: 13px; color: var(--color-text-primary); }
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

.det__main { display: flex; flex: 1; min-height: 0; min-width: 0; }

.det__work {
  display: grid;
  /* EQ column takes the room; plugins and I/O are narrow lists. */
  grid-template-columns: minmax(320px, 2fr) minmax(180px, 1fr) minmax(200px, 1fr);
  grid-template-rows: 1fr 1fr;
  gap: var(--spacing-sm);
  flex: 1;
  min-width: 0;
  padding: var(--spacing-sm);
  overflow: auto;
}
.det__eq      { grid-column: 1; grid-row: 1; }
.det__dyn     { grid-column: 1; grid-row: 2; }
.det__plugins { grid-column: 2; grid-row: 1 / span 2; }
.det__io      { grid-column: 3; grid-row: 1 / span 2; display: flex; flex-direction: column; gap: var(--spacing-sm); min-height: 0; }

/* Narrow window: one column, sections take the height they need and the work
   area scrolls. This is the "full height where needed" case — the EQ graph
   stops being squeezed into half a short window. */
@media (max-width: 1100px) {
  .det__work {
    grid-template-columns: 1fr;
    grid-template-rows: none;
    grid-auto-rows: min-content;
  }
  .det__eq, .det__dyn, .det__plugins, .det__io {
    grid-column: 1;
    grid-row: auto;
  }
}

.det__panel {
  display: flex;
  flex-direction: column;
  gap: var(--spacing-xs);
  padding: var(--spacing-sm);
  background: var(--color-surface);
  border: 1px solid var(--color-border);
  border-radius: var(--border-radius-md);
  min-height: 0;
}
/* Shells read as unfinished on purpose — a dashed edge and a label, so nothing
   here can be mistaken for a control that does something. */
.det__panel--pending { border-style: dashed; }

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

.det__plugins { overflow-y: auto; }
.det__insert {
  display: flex;
  align-items: center;
  gap: 6px;
  flex: 0 0 auto;
  font-size: 11px;
  padding: 5px 6px;
  color: var(--color-text-disabled);
  background: var(--color-background);
  border: 1px solid var(--color-border);
  border-radius: var(--border-radius-sm);
  cursor: not-allowed;
}
.det__insertnum { font-family: var(--font-mono); font-size: 9px; opacity: 0.6; }

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
.det__items {
  margin: 0;
  padding-left: 16px;
  font-size: 11px;
  color: var(--color-text-primary);
  overflow-y: auto;
}
.det__none { list-style: none; margin: 0; font-size: 11px; color: var(--color-text-disabled); }

.det__bank {
  display: flex;
  align-items: stretch;
  gap: var(--spacing-xs);
  flex: 0 0 auto;
  padding: var(--spacing-xs) var(--spacing-sm);
  border-top: 1px solid var(--color-border);
  background: var(--color-surface);
}
.det__banklist { display: flex; gap: 3px; flex: 1; min-width: 0; overflow-x: auto; }
.det__nav {
  display: flex;
  align-items: center;
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
  gap: 5px;
  padding: 4px 8px 4px 5px;
  color: var(--color-text-secondary);
  background: var(--color-background);
  border: 1px solid var(--color-border);
  border-radius: var(--border-radius-sm);
  cursor: pointer;
  white-space: nowrap;
}
.det__mini--active { border-color: var(--color-accent); color: var(--color-text-primary); }
/* Tall enough for a meter to be worth reading, short enough that the row stays
   a row. */
.det__minimeter { display: flex; height: 30px; flex: 0 0 auto; }
.det__minitext { display: flex; align-items: center; gap: 4px; font-size: 10px; }
.det__minichip { width: 6px; height: 6px; border-radius: 50%; flex: 0 0 auto; }
</style>

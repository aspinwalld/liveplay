<template>
  <!--
    Channel detail for one bus, zoned the way a channel-overview screen
    conventionally is: section tabs across the top, a control column on the
    left, processing in the centre, "what is connected to this" on the right,
    and a strip bank along the bottom for moving between channels without
    leaving the view.

    Overview and Output are real. EQ, Dynamics and Inserts are labelled
    placeholders on purpose — shipping the shell now means the DSP stage fills
    panels rather than inventing navigation late.
  -->
  <div class="det">
    <header class="det__head">
      <button class="det__nav" :disabled="!prevId" @click="$emit('select', prevId)">
        <span class="material-symbols-rounded">chevron_left</span>
      </button>
      <span class="det__chip" :style="{ background: bus.color || 'var(--color-accent)' }"></span>
      <input
        class="det__name"
        :value="bus.name"
        :disabled="bus.system"
        @change="$emit('patch', bus.id, { name: ($event.target as HTMLInputElement).value })"
      />
      <button class="det__nav" :disabled="!nextId" @click="$emit('select', nextId)">
        <span class="material-symbols-rounded">chevron_right</span>
      </button>

      <nav class="det__tabs">
        <button
          v-for="tab in tabs"
          :key="tab.id"
          class="det__tab"
          :class="{ 'det__tab--active': active === tab.id }"
          :disabled="tab.placeholder"
          :title="tab.placeholder ? t('mixer.comingSoon') : ''"
          @click="active = tab.id"
        >
          <span class="material-symbols-rounded">{{ tab.icon }}</span>
          <span>{{ t(tab.label) }}</span>
        </button>
      </nav>

      <div class="det__spacer"></div>
      <button
        v-if="!bus.system"
        class="det__delete"
        :title="t('mixer.deleteBus')"
        @click="$emit('delete', bus.id)"
      >
        <span class="material-symbols-rounded">delete</span>
      </button>
      <button class="det__close" @click="$emit('close')">
        <span class="material-symbols-rounded">close</span>
      </button>
    </header>

    <div class="det__body">
      <!-- Left: the channel's own controls. -->
      <section class="det__col">
        <h4 class="det__h">{{ t('mixer.channel') }}</h4>
        <div class="det__meterfader">
          <div class="det__meters" :style="{ height: METER_TRACK_PCT + '%' }">
            <!-- Same meter as the strip and the master: one set of zone
                 colours, one peak hold, one meter mode. -->
            <StereoMeter
              v-if="bus.mixerId"
              :mixer-id="bus.mixerId"
              :mono="bus.width < 2"
              bare
              :show-scale="false"
              :min-db="FADER_MIN_DB"
              :max-db="METER_MAX_DB"
            />
          </div>
          <MeterScale :min-db="FADER_MIN_DB" :max-db="FADER_MAX_DB" />
          <CanvasFader
            :db="bus.gainDb"
            :min-db="FADER_MIN_DB"
            :max-db="FADER_MAX_DB"
            :width="28"
            @input="(db: number) => $emit('patch', bus.id, { gainDb: db })"
            @reset="$emit('patch', bus.id, { gainDb: 0 })"
          />
        </div>
        <div class="det__gain">{{ gainLabel }} dB</div>
        <div class="det__pair">
          <button
            class="det__btn"
            :class="{ 'det__btn--on': bus.mute }"
            @click="$emit('patch', bus.id, { mute: !bus.mute })"
          >{{ t('mixer.mute') }}</button>
          <button class="det__btn" disabled :title="t('mixer.pflComingSoon')">{{ t('mixer.pfl') }}</button>
        </div>
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
      </section>

      <!-- Centre: processing. Placeholders until the insert chain exists. -->
      <section class="det__col det__col--wide">
        <h4 class="det__h">{{ t('mixer.processing') }}</h4>
        <div class="det__placeholder">
          <span class="material-symbols-rounded">equalizer</span>
          <p>{{ t('mixer.eqPlaceholder') }}</p>
        </div>
        <div class="det__placeholder">
          <span class="material-symbols-rounded">compress</span>
          <p>{{ t('mixer.dynPlaceholder') }}</p>
        </div>
      </section>

      <!-- Right: what this bus is connected to, both directions. -->
      <section class="det__col det__col--wide">
        <h4 class="det__h">{{ t('mixer.output') }}</h4>
        <select
          class="det__output"
          :value="outputValue"
          @change="onOutputChange"
        >
          <option value="master">{{ t('mixer.toMaster') }}</option>
          <option v-for="o in outputNames" :key="'out:' + o" :value="'out:' + o">{{ o }}</option>
        </select>
        <p v-if="unmapped" class="det__warn">
          {{ t('mixer.outputUnmapped', { name: bus.output.target }) }}
        </p>
        <p v-if="bus.output.type === 'bus'" class="det__warn">
          {{ t('mixer.busToBusUnsupported') }}
        </p>

        <h4 class="det__h">{{ t('mixer.feedingThis') }}</h4>
        <ul class="det__items">
          <li v-for="uuid in bus.itemUuids" :key="uuid">{{ itemName(uuid) }}</li>
          <li v-if="bus.itemUuids.length === 0" class="det__none">{{ t('mixer.nothingAssigned') }}</li>
        </ul>
      </section>
    </div>

    <!-- Strip bank: move between channels without leaving the view. -->
    <footer class="det__bank">
      <button
        v-for="b in buses"
        :key="b.id"
        class="det__mini"
        :class="{ 'det__mini--active': b.id === bus.id }"
        :disabled="!b.mixerId"
        @click="$emit('select', b.id)"
      >
        <span class="det__minichip" :style="{ background: b.color || 'var(--color-accent)' }"></span>
        <span class="det__mininame">{{ b.name }}</span>
      </button>
    </footer>
  </div>
</template>

<script setup lang="ts">
import { computed, ref } from 'vue';
import type { Bus } from '~/types/project';
import CanvasFader from './CanvasFader.vue';
import StereoMeter from './StereoMeter.vue';
import MeterScale from './MeterScale.vue';
import {
  FADER_MIN_DB, FADER_MAX_DB, METER_MAX_DB, METER_TRACK_PCT,
} from '~/utils/meterScale';

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

const tabs = [
  { id: 'overview', label: 'mixer.tabOverview', icon: 'tune',      placeholder: false },
  { id: 'eq',       label: 'mixer.tabEq',       icon: 'equalizer', placeholder: true  },
  { id: 'dyn',      label: 'mixer.tabDynamics', icon: 'compress',  placeholder: true  },
  { id: 'inserts',  label: 'mixer.tabInserts',  icon: 'extension', placeholder: true  },
];
const active = ref('overview');

const navigable = computed(() => props.buses.filter(b => b.mixerId));
const index     = computed(() => navigable.value.findIndex(b => b.id === props.bus.id));
const prevId    = computed(() => navigable.value[index.value - 1]?.id ?? '');
const nextId    = computed(() => navigable.value[index.value + 1]?.id ?? '');

const gainLabel = computed(() =>
  props.bus.gainDb <= -60 ? '-∞' : (props.bus.gainDb > 0 ? '+' : '') + props.bus.gainDb.toFixed(1));

const outputValue = computed(() =>
  props.bus.output.type === 'output' ? 'out:' + props.bus.output.target : 'master');

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
  border-top: 1px solid var(--color-border);
  background: var(--color-surface);
  max-height: 46%;
}

.det__head {
  display: flex;
  align-items: center;
  gap: var(--spacing-xs);
  padding: var(--spacing-xs) var(--spacing-sm);
  border-bottom: 1px solid var(--color-border);
}
.det__chip { width: 10px; height: 10px; border-radius: 50%; flex: 0 0 auto; }
.det__name {
  width: 140px;
  font-size: 13px;
  padding: 3px 6px;
  color: var(--color-text-primary);
  background: var(--color-background);
  border: 1px solid var(--color-border);
  border-radius: var(--border-radius-sm);
}
.det__nav, .det__delete, .det__close {
  display: flex;
  color: var(--color-text-secondary);
  background: none;
  border: none;
  cursor: pointer;
}
.det__nav:disabled { opacity: 0.3; cursor: default; }
.det__spacer { flex: 1; }

.det__tabs { display: flex; gap: 2px; margin-left: var(--spacing-md); }
.det__tab {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 11px;
  padding: 4px 8px;
  color: var(--color-text-secondary);
  background: none;
  border: none;
  border-bottom: 2px solid transparent;
  cursor: pointer;
}
.det__tab .material-symbols-rounded { font-size: 16px; }
.det__tab--active { color: var(--color-accent); border-bottom-color: var(--color-accent); }
.det__tab:disabled { opacity: 0.4; cursor: not-allowed; }

.det__body { display: flex; gap: var(--spacing-md); padding: var(--spacing-sm); overflow: auto; }
.det__col { display: flex; flex-direction: column; gap: var(--spacing-xs); min-width: 150px; }
.det__col--wide { flex: 1; }
.det__h {
  margin: 0;
  font-size: 10px;
  letter-spacing: 0.08em;
  text-transform: uppercase;
  color: var(--color-text-secondary);
}

.det__meterfader { display: flex; gap: var(--spacing-xs); height: 140px; }
.det__meters { display: flex; gap: 2px; align-self: flex-end; }
.det__gain { font-family: var(--font-mono); font-size: 12px; color: var(--color-text-primary); }
.det__pair { display: flex; gap: 4px; }
.det__btn {
  flex: 1;
  font-size: 11px;
  padding: 4px 0;
  color: var(--color-text-secondary);
  background: var(--color-background);
  border: 1px solid var(--color-border);
  border-radius: var(--border-radius-sm);
  cursor: pointer;
}
.det__btn--on { background: var(--color-danger); border-color: var(--color-danger); color: #fff; }
.det__btn:disabled { opacity: 0.4; cursor: not-allowed; }

.det__field { display: flex; align-items: center; justify-content: space-between; gap: 6px; font-size: 11px; color: var(--color-text-secondary); }
.det__output, .det__field select {
  font-size: 11px;
  padding: 3px;
  color: var(--color-text-primary);
  background: var(--color-background);
  border: 1px solid var(--color-border);
  border-radius: var(--border-radius-sm);
}

.det__placeholder {
  display: flex;
  align-items: center;
  gap: var(--spacing-sm);
  padding: var(--spacing-sm);
  color: var(--color-text-disabled);
  border: 1px dashed var(--color-border);
  border-radius: var(--border-radius-md);
}
.det__placeholder p { margin: 0; font-size: 11px; }

.det__warn { margin: 0; font-size: 11px; color: var(--color-warning); }
.det__items { margin: 0; padding-left: 16px; font-size: 11px; color: var(--color-text-primary); }
.det__none { list-style: none; margin-left: -16px; color: var(--color-text-disabled); }

.det__bank {
  display: flex;
  gap: 2px;
  padding: var(--spacing-xs);
  border-top: 1px solid var(--color-border);
  overflow-x: auto;
}
.det__mini {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 10px;
  padding: 3px 6px;
  color: var(--color-text-secondary);
  background: var(--color-background);
  border: 1px solid var(--color-border);
  border-radius: var(--border-radius-sm);
  cursor: pointer;
  white-space: nowrap;
}
.det__mini--active { border-color: var(--color-accent); color: var(--color-text-primary); }
.det__mini:disabled { opacity: 0.35; cursor: not-allowed; }
.det__minichip { width: 6px; height: 6px; border-radius: 50%; }
</style>

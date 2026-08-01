<template>
  <!--
    The mixer: a bank of channel strips, one per bus, with the master pinned
    to the right.

    Strips are buses, not cues. A cue is transient — it starts and stops
    constantly during a show — so a mixer whose strips appeared and vanished
    mid-show would be unusable. What feeds a bus is shown in the channel
    details view instead.
  -->
  <div class="mixer">
    <header class="mixer__bar">
      <h3 class="mixer__title">{{ t('mixer.title') }}</h3>
      <button class="mixer__add" @click="addBus">
        <span class="material-symbols-rounded">add</span>
        <span>{{ t('mixer.addBus') }}</span>
      </button>
      <div class="mixer__spacer"></div>
      <button
        class="mixer__close"
        :title="mode === 'side' ? t('mixer.expand') : t('mixer.dock')"
        @click="$emit('mode', mode === 'side' ? 'full' : 'side')"
      >
        <span class="material-symbols-rounded">
          {{ mode === 'side' ? 'open_in_full' : 'close_fullscreen' }}
        </span>
      </button>
      <button class="mixer__close" :title="t('mixer.close')" @click="$emit('close')">
        <span class="material-symbols-rounded">close</span>
      </button>
    </header>

    <div class="mixer__body">
      <div class="mixer__strips">
        <MixerStrip
          v-for="bus in userBuses"
          :key="bus.id"
          :bus="bus"
          :selected="bus.id === selectedId"
          :touch="touch"
          :output-names="outputNames"
          @select="selectedId = $event"
          @open="selectedId = $event"
          @patch="onPatch"
        />
        <p v-if="userBuses.length === 0" class="mixer__empty">{{ t('mixer.empty') }}</p>
      </div>

      <!-- Master, pinned right. A full strip, not just a meter: it keeps
           StereoMeter because that already has the clip latch, peak hold and
           gain-reduction sub-track that belong on a master. -->
      <div class="mixer__master">
        <div class="strip__inserts">
          <button v-for="n in 2" :key="n" class="strip__insert" :title="t('mixer.insertsComingSoon')" disabled>—</button>
        </div>
        <div class="mixer__masterout">{{ t('mixer.toHardware') }}</div>
        <div class="mixer__masterbody">
          <StereoMeter :left-index="0" :right-index="1" :min-db="-60" :max-db="0" :show-peak-value="true" />
          <CanvasFader
            :db="masterGainDb"
            :min-db="-60"
            :max-db="6"
            :width="touch ? 32 : 24"
            @input="onMasterGain"
            @reset="onMasterGain(0)"
          />
        </div>
        <div class="mixer__mastergain">{{ masterGainLabel }}</div>
        <div class="mixer__masterlabel">{{ t('mixer.master') }}</div>
      </div>
    </div>

    <!-- Selected bus detail. Overview and Output are real; the rest are
         labelled placeholders so Stage 5 fills panels rather than inventing
         navigation late. -->
    <MixerChannelDetails
      v-if="selectedBus"
      :bus="selectedBus"
      :buses="buses"
      :output-names="outputNames"
      @patch="onPatch"
      @delete="onDelete"
      @select="selectedId = $event"
      @close="selectedId = ''"
    />
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import type { Bus } from '~/types/project';
import MixerStrip from './MixerStrip.vue';
import MixerChannelDetails from './MixerChannelDetails.vue';
import StereoMeter from './StereoMeter.vue';
import CanvasFader from './CanvasFader.vue';

withDefaults(defineProps<{ mode?: 'side' | 'full' }>(), { mode: 'side' });
defineEmits<{ (e: 'close'): void; (e: 'mode', mode: 'side' | 'full'): void }>();

const server = useLiveplayServer();
const { t } = useLocalization();
const { uiMode } = useUiMode();
const touch = computed(() => uiMode.value === 'playback');

const selectedId  = ref('');
const outputNames = ref<string[]>([]);
const masterGainDb = ref(0);

const buses = computed<Bus[]>(() => server.buses ?? []);

// Only user buses get a strip. Main has no engine strip (unassigned cues take
// the legacy default-device path) and Monitor is the PFL destination, which
// nothing can be assigned to until PFL exists — drawing either as a fader that
// does nothing would be a lie, and Monitor showing up as the lone strip on a
// fresh project reads as "the mixer is already set up" when it isn't.
const userBuses = computed(() => buses.value.filter(b => !b.system && b.mixerId));

const masterGainLabel = computed(() =>
  masterGainDb.value <= -60 ? '-∞'
    : (masterGainDb.value > 0 ? '+' : '') + masterGainDb.value.toFixed(1));
const selectedBus = computed(() => buses.value.find(b => b.id === selectedId.value) ?? null);

onMounted(async () => {
  await server.fetchBuses();
  try {
    const map = await server.fetchOutputs();
    outputNames.value = (map?.outputs ?? []).map(o => o.name);
  } catch { outputNames.value = []; }
  try { masterGainDb.value = await server.fetchMasterGainDb(); } catch { /* keep 0 */ }
});

async function onPatch(id: string, patch: Partial<Bus>) {
  await server.patchBus(id, patch);
}
async function onDelete(id: string) {
  await server.deleteBus(id);
  if (selectedId.value === id) selectedId.value = '';
}
async function addBus() {
  const id = await server.createBus({ name: t('mixer.newBusName'), width: 2 });
  selectedId.value = id;
}
function onMasterGain(db: number) {
  masterGainDb.value = db;
  void server.setMasterGainDb(db);
}
</script>

<style scoped>
.mixer {
  display: flex;
  flex-direction: column;
  /* flex:1 + min-width:0 matter: as a lone item in the row-flex workspace the
     panel would otherwise shrink-wrap its content, leaving the rest of the
     container showing through un-themed. */
  flex: 1;
  min-width: 0;
  height: 100%;
  background: var(--color-background);
  overflow: hidden;
}

.mixer__bar {
  display: flex;
  align-items: center;
  gap: var(--spacing-sm);
  padding: var(--spacing-sm) var(--spacing-md);
  border-bottom: 1px solid var(--color-border);
}
.mixer__title { margin: 0; font-size: 14px; color: var(--color-text-primary); }
.mixer__spacer { flex: 1; }
.mixer__add,
.mixer__close {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 12px;
  padding: 4px 8px;
  color: var(--color-text-secondary);
  background: var(--color-surface);
  border: 1px solid var(--color-border);
  border-radius: var(--border-radius-sm);
  cursor: pointer;
}
.mixer__add:hover, .mixer__close:hover { color: var(--color-text-primary); }

.mixer__body { display: flex; flex: 1; min-height: 0; min-width: 0; }
.mixer__strips {
  display: flex;
  gap: var(--spacing-xs);
  padding: var(--spacing-sm);
  /* min-width:0 is what actually lets this scroll: without it the flex item
     refuses to shrink below its content and the strips squash instead. */
  flex: 1;
  min-width: 0;
  overflow-x: auto;
  align-items: stretch;
}
.mixer__empty {
  align-self: center;
  color: var(--color-text-disabled);
  font-size: 12px;
}

.mixer__master {
  display: flex;
  flex-direction: column;
  gap: var(--spacing-xs);
  flex: 0 0 auto;
  padding: var(--spacing-sm);
  border-left: 1px solid var(--color-border);
  background: var(--color-surface);
}
.mixer__masterbody { display: flex; gap: var(--spacing-xs); flex: 1; min-height: 140px; justify-content: center; }
.mixer__masterout {
  text-align: center;
  font-size: 10px;
  padding: 2px;
  color: var(--color-text-disabled);
  background: var(--color-background);
  border: 1px solid var(--color-border);
  border-radius: var(--border-radius-sm);
}
.mixer__mastergain {
  text-align: center;
  font-family: var(--font-mono);
  font-size: 11px;
  color: var(--color-text-primary);
}
.mixer__masterlabel {
  text-align: center;
  font-size: 11px;
  letter-spacing: 0.08em;
  padding: 3px;
  background: var(--color-background);
  border-radius: var(--border-radius-sm);
  color: var(--color-text-secondary);
}

/* Shared with MixerStrip so the master's insert slots match the channels'. */
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
</style>

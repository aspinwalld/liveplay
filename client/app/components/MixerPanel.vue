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

      <!-- Master, pinned right. Uses StereoMeter rather than the strip meter:
           it already has the clip latch, peak hold and gain-reduction
           sub-track that belong on a master. -->
      <div class="mixer__master">
        <div class="mixer__masterlabel">{{ t('mixer.master') }}</div>
        <StereoMeter :left-index="0" :right-index="1" :min-db="-60" :max-db="0" :show-peak-value="true" />
        <VolumeSlider
          :db="masterGainDb"
          :min-db="-60"
          :max-db="6"
          :title="t('mixer.master')"
          @input="onMasterGain"
          @reset="onMasterGain(0)"
        />
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
import VolumeSlider from './VolumeSlider.vue';

defineEmits<{ (e: 'close'): void }>();

const server = useLiveplayServer();
const { t } = useLocalization();
const { uiMode } = useUiMode();
const touch = computed(() => uiMode.value === 'playback');

const selectedId  = ref('');
const outputNames = ref<string[]>([]);
const masterGainDb = ref(0);

const buses = computed<Bus[]>(() => server.buses ?? []);
// Main has no strip of its own yet — it is the engine's default mixer and
// unassigned cues route through the legacy default-device path — so showing it
// as a fader that does nothing would be a lie.
const userBuses = computed(() => buses.value.filter(b => b.id !== 'main' && b.mixerId));
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

.mixer__body { display: flex; flex: 1; min-height: 0; }
.mixer__strips {
  display: flex;
  gap: var(--spacing-xs);
  padding: var(--spacing-sm);
  overflow-x: auto;
  flex: 1;
}
.mixer__empty {
  align-self: center;
  color: var(--color-text-disabled);
  font-size: 12px;
}

.mixer__master {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: var(--spacing-xs);
  padding: var(--spacing-sm);
  border-left: 1px solid var(--color-border);
  background: var(--color-surface);
}
.mixer__masterlabel {
  font-size: 11px;
  letter-spacing: 0.08em;
  color: var(--color-text-secondary);
}
</style>

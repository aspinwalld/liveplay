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

      <!-- Detached: the window IS the mixer, so the side/full toggle has
           nothing to toggle between and the only exit is back to the main
           window. Docked: offer detach, the size toggle, and close. -->
      <template v-if="!detached">
        <button v-if="canDetach" class="mixer__close" :title="t('mixer.detach')" @click="detach">
          <span class="material-symbols-rounded">open_in_new</span>
        </button>
        <button
          class="mixer__close"
          :title="mode === 'side' ? t('mixer.expand') : t('mixer.dock')"
          @click="$emit('mode', mode === 'side' ? 'full' : 'side')"
        >
          <span class="material-symbols-rounded">
            {{ mode === 'side' ? 'open_in_full' : 'close_fullscreen' }}
          </span>
        </button>
      </template>

      <button
        class="mixer__close"
        :title="detached ? t('mixer.dockToMain') : t('mixer.close')"
        @click="$emit('close')"
      >
        <span class="material-symbols-rounded">
          {{ detached ? 'dock_to_left' : 'close' }}
        </span>
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
          @open="openDetails"
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
          <!-- Same geometry as the channel strips: the meter is dBFS and tops
               out at 0, so its track covers only that part of the fader's
               range and 0 dBFS lands on the shared scale's 0 tick. -->
          <div class="mixer__mastermeter" :style="{ height: METER_TRACK_PCT + '%' }">
            <StereoMeter
              :left-index="0"
              :right-index="1"
              :min-db="FADER_MIN_DB"
              :max-db="METER_MAX_DB"
              :show-peak-value="true"
            />
          </div>
          <MeterScale :min-db="FADER_MIN_DB" :max-db="FADER_MAX_DB" />
          <CanvasFader
            :db="masterGainDb"
            :min-db="FADER_MIN_DB"
            :max-db="FADER_MAX_DB"
            :width="touch ? 32 : 24"
            @input="onMasterGain"
            @reset="onMasterGain(0)"
          />
        </div>
        <div class="mixer__mastergain">{{ masterGainLabel }}</div>
        <div class="mixer__masterlabel">{{ t('mixer.master') }}</div>
      </div>
    </div>

    <!-- Selected bus detail. Only in full mode: the three-column layout is
         unusable squeezed into a docked side pane, so opening it switches the
         mixer to full width rather than rendering something cramped. -->
    <MixerChannelDetails
      v-if="selectedBus && mode === 'full'"
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
import MeterScale from './MeterScale.vue';
import {
  FADER_MIN_DB, FADER_MAX_DB, METER_MAX_DB, METER_TRACK_PCT,
} from '~/utils/meterScale';

const props = withDefaults(
  defineProps<{ mode?: 'side' | 'full'; detached?: boolean }>(),
  { mode: 'side', detached: false },
);
const emit = defineEmits<{ (e: 'close'): void; (e: 'mode', mode: 'side' | 'full'): void }>();

// Detaching is an Electron affordance — in a browser there is no second
// window to open, so the button simply isn't offered there.
const canDetach = computed(() =>
  import.meta.client && !!(window as any).electronAPI?.openMixerWindow);

// The main window flips its own panel off when it hears `mixer-window-opened`,
// so we don't touch mixerOpen here — that keeps the detach path identical
// whether the window is spawned from this button or reopened later.
function detach() {
  void (window as any).electronAPI?.openMixerWindow?.();
}

// Opening channel details from a docked pane switches to full width first —
// the detail layout needs the room, and silently rendering it crushed was
// worse than not showing it.
function openDetails(id: string) {
  selectedId.value = id;
  if (props.mode !== 'full') emit('mode', 'full');
}

const server = useLiveplayServer();
const { t } = useLocalization();
const { uiMode } = useUiMode();
const touch = computed(() => uiMode.value === 'playback');

// Shared rather than local: switching between docked and full swaps which
// MixerPanel instance is mounted, so a local ref would be destroyed with the
// old one and the selection would be lost exactly when opening details forces
// that switch — which looked like needing to click twice.
const selectedId  = useState<string>('liveplay:mixerSelectedBus', () => '');
const outputNames = ref<string[]>([]);

// The master strip drives the *same* parameter as the transport bar's Main
// fader — the output-channel gain on masters 0/1 — rather than the engine's
// global master gain. Two faders both labelled master that moved independently
// was just confusing. outputChannelGains is reactive and kept live by the
// output_channel_gain_changed broadcast, so the two track each other in both
// directions and across clients.
const masterGainDb = computed(() => server.outputChannelGains[0] ?? 0);

const buses = computed<Bus[]>(() => server.buses ?? []);

// Only user buses get a strip. Main is where unassigned cues already land and
// Monitor is the PFL destination, which nothing can be assigned to until PFL
// exists — drawing either as a fader that does nothing would be a lie, and
// Monitor showing up as the lone strip on a fresh project reads as "the mixer
// is already set up" when it isn't.
//
// Deliberately NOT filtered on mixerId. A bus is a bus whether or not the
// engine currently has a strip for it; MixerStrip already renders the
// strip-less state. Hiding them meant a single fetch that caught the server
// mid-rebuild emptied the whole rail, and nothing refetched until the panel
// was remounted — which is why expanding or undocking appeared to "fix" it.
const userBuses = computed(() => buses.value.filter(b => !b.system));

const masterGainLabel = computed(() =>
  masterGainDb.value <= -60 ? '-∞'
    : (masterGainDb.value > 0 ? '+' : '') + Number(masterGainDb.value).toFixed(1));
const selectedBus = computed(() => buses.value.find(b => b.id === selectedId.value) ?? null);

onMounted(async () => {
  await server.fetchBuses();
  try {
    const map = await server.fetchOutputs();
    outputNames.value = (map?.outputs ?? []).map(o => o.name);
  } catch { outputNames.value = []; }
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
  // Both channels of the pair move together, matching the transport bar.
  void server.setOutputChannelGainDb(0, db);
  void server.setOutputChannelGainDb(1, db);
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
.mixer__masterbody { display: flex; gap: 3px; flex: 1; min-height: 150px; justify-content: center; }
.mixer__mastermeter { display: flex; align-self: flex-end; }
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

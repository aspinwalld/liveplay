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

      <!-- Master, pinned right — the same component as every other strip, so
           it has the same rows at the same heights and its fader lines up
           with theirs. It was bespoke markup here, which is exactly why the
           one strip that matters most looked unlike all the others. -->
      <div class="mixer__master">
        <MixerStrip :bus="masterBus" :touch="touch" :output-names="[]" master />
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

// The master presented as a bus, so it can go through MixerStrip. Nothing on
// the server backs this — MixerStrip reads `master` and takes the output-pair
// path for the fader and the meter — but shaping it as a Bus keeps one strip
// component instead of two that drift apart.
const masterBus = computed<Bus>(() => ({
  id: '__master__',
  name: t('mixer.master'),
  color: '',
  order: Number.MAX_SAFE_INTEGER,
  width: 2,
  gainDb: masterGainDb.value,
  mute: false,
  pan: 0,
  system: true,
  output: { type: 'output', target: '' },
  mixerId: '',
  itemUuids: [],
}));

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
  /* min-height:0 completes the chain that lets a strip's fader shrink with the
     pane instead of the strips overflowing. */
  min-height: 0;
  overflow-x: auto;
  align-items: stretch;
}
.mixer__empty {
  align-self: center;
  color: var(--color-text-disabled);
  font-size: 12px;
}

/* The master is just a strip in a divider; the padding matches .mixer__strips
   so its rows sit at exactly the same heights as the channels'. */
.mixer__master {
  display: flex;
  flex: 0 0 auto;
  min-height: 0;
  padding: var(--spacing-sm);
  border-left: 1px solid var(--color-border);
  background: var(--color-surface);
}
</style>

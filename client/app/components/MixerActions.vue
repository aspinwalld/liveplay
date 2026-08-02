<template>
  <!--
    The mixer's own controls: add a bus, and the window buttons.

    They live in a footer rather than a title bar because a mixer is judged on
    how much of the window is fader. A header with a title and a labelled
    button cost a row of height on every view and told the operator something
    they already knew; these ride along the bottom bar the channel view needs
    anyway, and give the rail one slim strip instead.
  -->
  <div class="acts">
    <button class="acts__btn acts__btn--add" :title="t('mixer.addBus')" @click="$emit('add')">
      <span class="material-symbols-rounded">add</span>
    </button>

    <!-- Detached: the window IS the mixer, so the side/full toggle has nothing
         to toggle between and the only exit is back to the main window. -->
    <template v-if="!detached">
      <button v-if="canDetach" class="acts__btn" :title="t('mixer.detach')" @click="$emit('detach')">
        <span class="material-symbols-rounded">open_in_new</span>
      </button>
      <button
        class="acts__btn"
        :title="mode === 'side' ? t('mixer.expand') : t('mixer.dock')"
        @click="$emit('mode', mode === 'side' ? 'full' : 'side')"
      >
        <span class="material-symbols-rounded">
          {{ mode === 'side' ? 'open_in_full' : 'close_fullscreen' }}
        </span>
      </button>
    </template>

    <button
      class="acts__btn"
      :title="detached ? t('mixer.dockToMain') : t('mixer.close')"
      @click="$emit('close')"
    >
      <span class="material-symbols-rounded">{{ detached ? 'dock_to_left' : 'close' }}</span>
    </button>
  </div>
</template>

<script setup lang="ts">
defineProps<{
  mode: 'side' | 'full';
  detached?: boolean;
  canDetach?: boolean;
}>();

defineEmits<{
  (e: 'add'): void;
  (e: 'detach'): void;
  (e: 'mode', mode: 'side' | 'full'): void;
  (e: 'close'): void;
}>();

const { t } = useLocalization();
</script>

<style scoped>
.acts { display: flex; align-items: center; gap: 3px; flex: 0 0 auto; }
.acts__btn {
  display: flex;
  align-items: center;
  padding: 4px;
  color: var(--color-text-secondary);
  background: var(--color-background);
  border: 1px solid var(--color-border);
  border-radius: var(--border-radius-sm);
  cursor: pointer;
}
.acts__btn:hover { color: var(--color-text-primary); }
.acts__btn .material-symbols-rounded { font-size: 18px; }
/* Adding a bus is the one thing here that changes the show, so it reads as an
   action rather than a window control. */
.acts__btn--add { color: var(--color-accent); border-color: var(--color-accent); }
</style>

<template>
  <!--
    Canvas-based rotary control.

    Built the way CanvasFader is built, and for the same reason: canvas gives
    pixel-exact control of the arc, the pointer and the theming, and CSS
    variables are read at draw time so the control re-themes without a
    remount. Anything drawn on a canvas has to do that — a canvas cannot
    inherit CSS.

    The value is a plain number in whatever unit the caller works in (pan
    -1..+1 now, dB / Hz / Q for EQ later), so nothing about dB is baked in.

    Interaction matches CanvasFader deliberately — an operator who has learned
    one control should not have to learn the other:
      * Click + vertical drag       -> set value (coarse)
      * Shift + drag                -> fine adjust (quarter sensitivity)
      * Wheel                       -> one step
      * Double-click                -> emit('reset')
  -->
  <div
    ref="hostRef"
    class="knob"
    :class="{ 'knob--disabled': disabled }"
    :style="{ width: size + 'px', height: size + 'px' }"
    :title="title"
    @mousedown="onMouseDown"
    @dblclick="!disabled && $emit('reset')"
    @wheel.prevent="onWheel"
  >
    <canvas ref="canvasRef" class="knob__canvas" />
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref, watch } from 'vue';

const props = withDefaults(defineProps<{
  value: number;
  min?: number;
  max?: number;
  /**
   * Where the value arc grows from. Centre-origin for a bipolar control like
   * pan; set it to `min` for a unipolar one like Q or a mix amount.
   */
  origin?: number;
  step?: number;
  fineStep?: number;
  /** Diameter in px. */
  size?: number;
  disabled?: boolean;
  title?: string;
}>(), {
  min: -1,
  max: 1,
  origin: 0,
  step: 0.02,
  fineStep: 0.005,
  size: 34,
  disabled: false,
  title: '',
});

const emit = defineEmits<{
  (e: 'input', value: number): void;
  (e: 'reset'): void;
}>();

const hostRef   = ref<HTMLElement | null>(null);
const canvasRef = ref<HTMLCanvasElement | null>(null);

const range = computed(() => props.max - props.min);

// The dial's travel: 270 degrees with the gap at the bottom, which is where
// every hardware pot puts it.
const START_ANGLE = Math.PI * 0.75;
const SWEEP       = Math.PI * 1.5;

const norm = (v: number) =>
  Math.max(0, Math.min(1, (v - props.min) / (range.value || 1)));
const angleFor = (v: number) => START_ANGLE + norm(v) * SWEEP;

function readCssVar(name: string, fallback: string): string {
  const host = hostRef.value;
  if (!host) return fallback;
  const v = getComputedStyle(host).getPropertyValue(name).trim();
  return v || fallback;
}

function draw() {
  const cv = canvasRef.value;
  const host = hostRef.value;
  if (!cv || !host) return;

  const dpr = window.devicePixelRatio || 1;
  const w = host.clientWidth;
  const h = host.clientHeight;
  if (w === 0 || h === 0) return;
  if (cv.width !== Math.round(w * dpr) || cv.height !== Math.round(h * dpr)) {
    cv.width  = Math.round(w * dpr);
    cv.height = Math.round(h * dpr);
  }
  const ctx = cv.getContext('2d');
  if (!ctx) return;
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);

  const border    = readCssVar('--color-border', '#444');
  const accent    = readCssVar('--color-accent', '#0f62fe');
  const disabledC = readCssVar('--color-text-disabled', '#666');
  const pointerC  = readCssVar('--color-text-primary', '#eee');

  const ringW = Math.max(2, Math.round(props.size * 0.09));
  const cx = w / 2;
  const cy = h / 2;
  const r  = (Math.min(w, h) - ringW) / 2 - 1;
  if (r <= 0) return;

  // Track: the full travel, always visible so the range is legible at rest.
  ctx.strokeStyle = border;
  ctx.lineWidth = ringW;
  ctx.lineCap = 'round';
  ctx.beginPath();
  ctx.arc(cx, cy, r, START_ANGLE, START_ANGLE + SWEEP);
  ctx.stroke();

  // Value arc, drawn from the origin so a bipolar control reads as a
  // deflection either side of centre rather than a fill from the left stop.
  const a0 = angleFor(props.origin);
  const a1 = angleFor(props.value);
  if (Math.abs(a1 - a0) > 0.001) {
    ctx.strokeStyle = props.disabled ? disabledC : accent;
    ctx.beginPath();
    ctx.arc(cx, cy, r, Math.min(a0, a1), Math.max(a0, a1));
    ctx.stroke();
  }

  // Origin tick, so centre is findable without reading the number.
  ctx.strokeStyle = readCssVar('--color-text-secondary', '#888');
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(cx + Math.cos(a0) * (r - ringW), cy + Math.sin(a0) * (r - ringW));
  ctx.lineTo(cx + Math.cos(a0) * (r + ringW * 0.6), cy + Math.sin(a0) * (r + ringW * 0.6));
  ctx.stroke();

  // Pointer
  ctx.strokeStyle = props.disabled ? disabledC : pointerC;
  ctx.lineWidth = Math.max(2, ringW - 1);
  ctx.lineCap = 'round';
  ctx.beginPath();
  ctx.moveTo(cx + Math.cos(a1) * (r * 0.3), cy + Math.sin(a1) * (r * 0.3));
  ctx.lineTo(cx + Math.cos(a1) * (r - ringW * 0.9), cy + Math.sin(a1) * (r - ringW * 0.9));
  ctx.stroke();
}

watch(() => [props.value, props.min, props.max, props.origin, props.disabled], () => draw());

let resizeObserver: ResizeObserver | null = null;
let themeObserver: MutationObserver | null = null;
onMounted(() => {
  draw();
  if (hostRef.value) {
    resizeObserver = new ResizeObserver(() => draw());
    resizeObserver.observe(hostRef.value);
  }
  // Same reason as CanvasFader: the CSS variables above are sampled at draw
  // time, so a theme flip has to trigger a redraw explicitly.
  themeObserver = new MutationObserver(() => draw());
  themeObserver.observe(document.documentElement, { attributes: true, attributeFilter: ['data-theme'] });
});
onUnmounted(() => {
  if (resizeObserver) resizeObserver.disconnect();
  resizeObserver = null;
  if (themeObserver) themeObserver.disconnect();
  themeObserver = null;
});

// ---- Pointer interaction ---------------------------------------------------
// Anchored vertical drag, as on the fader: mouse-down records the value and
// the Y position, and moves translate the delta. A knob is far too small to
// map its own height to the range, so full travel is a fixed drag distance —
// the convention every plugin UI uses.
const DRAG_TRAVEL_PX = 140;

let dragging    = false;
let dragStartY  = 0;
let dragStartVal = 0;

function onMouseDown(e: MouseEvent) {
  if (props.disabled || e.button !== 0) return;
  dragging = true;
  dragStartY = e.clientY;
  dragStartVal = props.value;
  window.addEventListener('mousemove', onMouseMove);
  window.addEventListener('mouseup', onMouseUp);
}

function onMouseMove(e: MouseEvent) {
  if (!dragging) return;
  const dy = dragStartY - e.clientY;      // dragging up = positive
  const sens = e.shiftKey ? 0.25 : 1;
  const perPx = (range.value / DRAG_TRAVEL_PX) * sens;
  emit('input', clampToStep(dragStartVal + dy * perPx, e.shiftKey));
}

function onMouseUp() {
  dragging = false;
  window.removeEventListener('mousemove', onMouseMove);
  window.removeEventListener('mouseup', onMouseUp);
}

function onWheel(e: WheelEvent) {
  if (props.disabled) return;
  const step = e.shiftKey ? props.fineStep : props.step;
  const dir  = e.deltaY < 0 ? 1 : -1;
  emit('input', clampToStep(props.value + dir * step, e.shiftKey));
}

function clampToStep(v: number, fine: boolean): number {
  const step = fine ? props.fineStep : props.step;
  const snapped = step > 0 ? Math.round(v / step) * step : v;
  const clamped = Math.max(props.min, Math.min(props.max, snapped));
  // Avoid -0 noise, and the float dust that rounding to a fractional step
  // leaves behind (0.02 * 3 = 0.06000000000000001).
  return Object.is(clamped, -0) ? 0 : Number(clamped.toFixed(4));
}
</script>

<style scoped>
.knob {
  flex: 0 0 auto;
  cursor: pointer;
  user-select: none;
  touch-action: none;
}
.knob--disabled { cursor: not-allowed; }
.knob__canvas {
  display: block;
  width: 100%;
  height: 100%;
}
</style>

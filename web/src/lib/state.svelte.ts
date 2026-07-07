// Single reactive store — the client mirror of the device's authoritative
// state. The device pushes patches; we apply them here. UI controls read this
// store and write through `patchScene()`, which sends to the device and waits
// for the echo to update local state (so there are no feedback loops).
import type { RGB } from './color';

// Global control bitmask (matches the firmware's global_param_t bits).
export const GLOBAL = {
  COLOR: 1,
  SPEED: 2,
  AUDIO_SENS: 4,
  BRIGHTNESS: 8,
  STRETCH: 16,
} as const;

export interface Scene {
  power: boolean;
  r: number;
  g: number;
  b: number;
  brightness: number;
  stretch: number;
  speed: number;
  audioSens: number;
  effectId: number;
  params: number[];
}

export interface EffectParamDef {
  name: string;
  min: number;
  max: number;
  def: number;
}

export interface EffectDef {
  id: string;
  name: string;
  globals: number; // bitmask
  params: EffectParamDef[];
}

export interface NetInfo {
  mode: 'boot' | 'connecting' | 'sta' | 'ap' | string;
  ssid: string;
  hostname: string;
  ip: string;
}

export interface Metrics {
  vin: number;
  laserTemp: number;
  mcuTemp: number;
  fanRpm: number;
  fanDuty: number;
  pdCurrent: number;
  pdOk: boolean;
  warn: string[];
  err: string[];
}

export interface AccessPoint {
  ssid: string;
  rssi: number;
  auth: number;
}

export type ConnStatus = 'connecting' | 'open' | 'closed';

// Partial scene update — keys sent over the wire in a patch.
export type ScenePatch = Partial<Scene>;

function emptyScene(): Scene {
  return {
    power: false,
    r: 1,
    g: 1,
    b: 1,
    brightness: 0.8,
    stretch: 0,
    speed: 0.5,
    audioSens: 0.5,
    effectId: 0,
    params: [],
  };
}

class Store {
  conn = $state<ConnStatus>('connecting');
  seq = $state(0);
  scene = $state<Scene>(emptyScene());
  effects = $state<EffectDef[]>([]);
  net = $state<NetInfo>({ mode: 'boot', ssid: '', hostname: '', ip: '' });
  metrics = $state<Metrics | null>(null);
  pwmHz = $state(120);
  aps = $state<AccessPoint[]>([]);
  scanning = $state(false);
  lastError = $state<{ code: number; msg: string } | null>(null);

  // Sender installed by the ws layer. Returns true if the message was sent.
  send: (msg: unknown) => boolean = () => false;

  // Shared color object the three color controls read/write.
  get rgb(): RGB {
    return { r: this.scene.r, g: this.scene.g, b: this.scene.b };
  }

  // The currently selected effect definition (effectId is a catalog index).
  get currentEffect(): EffectDef | undefined {
    return this.effects[this.scene.effectId];
  }

  applySnapshot(msg: {
    seq: number;
    scene: Scene;
    effects: EffectDef[];
    net: NetInfo;
    pwmHz?: number;
  }): void {
    this.seq = msg.seq;
    this.scene = { ...emptyScene(), ...msg.scene };
    this.effects = msg.effects ?? [];
    this.net = msg.net ?? this.net;
    this.pwmHz = msg.pwmHz ?? this.pwmHz;
  }

  applyPatch(seq: number, set: ScenePatch): void {
    this.seq = seq;
    this.scene = { ...this.scene, ...set };
  }

  applyNet(net: NetInfo): void {
    this.net = net;
  }

  applyMetrics(m: Metrics): void {
    this.metrics = m;
  }
}

export const store = new Store();

// ---- Patch helpers (write path) --------------------------------------------
// We send only changed fields and apply them locally right away for
// responsiveness. The device broadcasts every applied patch tagged with the
// originator's cid; the ws layer skips the fields of our own echoes (local
// state is already ahead of them) and applies everyone else's, so all clients
// converge on the device's authoritative state without echo jitter.

export function patchScene(set: ScenePatch): void {
  // Optimistic local apply for responsiveness; echo will reconcile.
  store.scene = { ...store.scene, ...set };
  store.send({ type: 'patch', set });
}

export function setRgb(rgb: RGB): void {
  patchScene({ r: rgb.r, g: rgb.g, b: rgb.b });
}

export function setEffect(index: number): void {
  const eff = store.effects[index];
  // Initialize params to declared defaults when switching effects.
  const params = eff ? eff.params.map((p) => p.def) : [];
  patchScene({ effectId: index, params });
}

export function setParam(i: number, value: number): void {
  const params = store.scene.params.slice();
  params[i] = value;
  patchScene({ params });
}

export function getSelectedEffect(): EffectDef | undefined {
  return store.currentEffect;
}

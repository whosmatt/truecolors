// WebSocket transport. One persistent socket to /ws. The device is the single
// source of truth: it pushes snapshots, patches, net updates and 1 Hz metrics.
// We reconnect automatically with exponential backoff and detect seq gaps
// (requesting a fresh snapshot to resync).
import {
  store,
  type ScenePatch,
  type Scene,
  type EffectDef,
  type NetInfo,
  type Metrics,
  type AccessPoint,
} from './state.svelte';

const MIN_BACKOFF = 500;
const MAX_BACKOFF = 8000;

// Outgoing patch throttle (~50 Hz)
const SEND_INTERVAL_MS = 20;

// Per-session echo tag
const CID = ((Math.random() * 0xffffffff) >>> 0) || 1;

let socket: WebSocket | null = null;
let backoff = MIN_BACKOFF;
let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
let manualClose = false;

// Coalesced outgoing scene patch buffer + flush timer.
let pendingPatch: ScenePatch | null = null;
let flushTimer: ReturnType<typeof setTimeout> | null = null;

function wsUrl(): string {
  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
  return `${proto}//${location.host}/ws`;
}

function flushPatch(): void {
  flushTimer = null;
  if (!pendingPatch) return;
  const set = pendingPatch;
  pendingPatch = null;
  rawSend({ type: 'patch', set, cid: CID });
}

function rawSend(msg: unknown): boolean {
  if (socket && socket.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify(msg));
    return true;
  }
  return false;
}

// Public send. Scene patches are coalesced+throttled; everything else goes
// out immediately.
function send(msg: unknown): boolean {
  if (
    msg &&
    typeof msg === 'object' &&
    (msg as { type?: string }).type === 'patch'
  ) {
    const set = (msg as { set: ScenePatch }).set;
    pendingPatch = { ...(pendingPatch ?? {}), ...set };
    if (!flushTimer) flushTimer = setTimeout(flushPatch, SEND_INTERVAL_MS);
    return socket?.readyState === WebSocket.OPEN;
  }
  return rawSend(msg);
}

interface SnapshotMsg {
  type: 'snapshot';
  seq: number;
  scene: Scene;
  effects: EffectDef[];
  net: NetInfo;
  pwmHz?: number;
}
interface PwmHzMsg {
  type: 'pwm_hz';
  hz: number;
}
interface PatchMsg {
  type: 'patch';
  seq: number;
  src: number;
  origin?: number;
  set: ScenePatch;
}
interface NetMsg extends NetInfo {
  type: 'net';
}
interface MetricsMsg extends Metrics {
  type: 'metrics';
}
interface WifiListMsg {
  type: 'wifi_list';
  aps: AccessPoint[];
}
interface ErrorMsg {
  type: 'error';
  code: number;
  msg: string;
}

type ServerMsg =
  | SnapshotMsg
  | PatchMsg
  | NetMsg
  | MetricsMsg
  | WifiListMsg
  | PwmHzMsg
  | ErrorMsg;

function handleMessage(raw: string): void {
  let msg: ServerMsg;
  try {
    msg = JSON.parse(raw) as ServerMsg;
  } catch {
    return;
  }
  switch (msg.type) {
    case 'snapshot':
      store.applySnapshot(msg);
      break;
    case 'patch': {
      // Seq must advance by exactly 1; a gap means we missed a patch, apply
      // what we got, then request a fresh snapshot to resync.
      const gap = store.seq !== 0 && msg.seq !== store.seq + 1;
      // Our own echo: local state is already ahead of it
      store.applyPatch(msg.seq, msg.origin === CID ? {} : msg.set);
      if (gap) requestSnapshot();
      break;
    }
    case 'net':
      store.applyNet({
        mode: msg.mode,
        ssid: msg.ssid,
        hostname: msg.hostname,
        ip: msg.ip,
      });
      break;
    case 'metrics':
      store.applyMetrics({
        vin: msg.vin,
        laserTemp: msg.laserTemp,
        mcuTemp: msg.mcuTemp,
        fanRpm: msg.fanRpm,
        fanDuty: msg.fanDuty,
        pdCurrent: msg.pdCurrent,
        pdOk: msg.pdOk,
        warn: msg.warn ?? [],
        err: msg.err ?? [],
      });
      break;
    case 'wifi_list':
      store.aps = msg.aps ?? [];
      store.scanning = false;
      break;
    case 'pwm_hz':
      store.pwmHz = msg.hz;
      break;
    case 'error':
      store.lastError = { code: msg.code, msg: msg.msg };
      break;
  }
}

function scheduleReconnect(): void {
  if (manualClose || reconnectTimer) return;
  reconnectTimer = setTimeout(() => {
    reconnectTimer = null;
    connect();
  }, backoff);
  backoff = Math.min(backoff * 2, MAX_BACKOFF);
}

export function connect(): void {
  manualClose = false;
  if (
    socket &&
    (socket.readyState === WebSocket.OPEN ||
      socket.readyState === WebSocket.CONNECTING)
  ) {
    return;
  }
  store.conn = 'connecting';
  let ws: WebSocket;
  try {
    ws = new WebSocket(wsUrl());
  } catch {
    scheduleReconnect();
    return;
  }
  socket = ws;

  ws.onopen = () => {
    backoff = MIN_BACKOFF;
    store.conn = 'open';
    // Ask for a fresh snapshot on (re)connect to resync state.
    requestSnapshot();
  };
  ws.onmessage = (ev) => handleMessage(ev.data as string);
  ws.onclose = () => {
    if (socket === ws) socket = null;
    store.conn = 'closed';
    scheduleReconnect();
  };
  ws.onerror = () => {
    // onclose follows; let it handle reconnect.
    ws.close();
  };
}

export function disconnect(): void {
  manualClose = true;
  if (reconnectTimer) {
    clearTimeout(reconnectTimer);
    reconnectTimer = null;
  }
  socket?.close();
  socket = null;
}

export function requestSnapshot(): void {
  rawSend({ type: 'get_snapshot' });
}

// Wire the store's sender to this transport.
store.send = send;

// ---- Admin commands (over WS) ----------------------------------------------
export function wifiScan(): void {
  store.scanning = true;
  rawSend({ type: 'wifi_scan' });
}
export function wifiProvision(ssid: string, pass: string): void {
  rawSend({ type: 'wifi_provision', ssid, pass });
}
export function wifiAp(): void {
  rawSend({ type: 'wifi_ap' });
}
export function reboot(): void {
  rawSend({ type: 'reboot' });
}
export function setPwmHz(hz: number): void {
  rawSend({ type: 'set_pwm_hz', hz });
}
export function factoryReset(): void {
  rawSend({ type: 'factory_reset' });
}

// ---- OTA firmware update ---------------------------------------------------
// The one non-WS path: stream the raw .bin to POST /api/ota. We use XHR (not
// fetch) for upload progress. The device replies "ok" then reboots, so onDone
// fires just before the socket drops.
export interface OtaHandlers {
  onProgress?: (frac: number) => void;
  onDone?: () => void;
  onError?: (msg: string) => void;
}

export function otaUpdate(file: Blob, h: OtaHandlers = {}): () => void {
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/api/ota');
  xhr.upload.onprogress = (e) => {
    if (e.lengthComputable) h.onProgress?.(e.loaded / e.total);
  };
  xhr.onload = () => {
    if (xhr.status >= 200 && xhr.status < 300) h.onDone?.();
    else h.onError?.(`HTTP ${xhr.status}: ${xhr.responseText || 'upload failed'}`);
  };
  xhr.onerror = () => h.onError?.('network error during upload');
  xhr.send(file);
  return () => xhr.abort();
}

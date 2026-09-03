import test from 'node:test';
import assert from 'node:assert/strict';
import { chooseMapMode, projectStations, MapSurface } from '../js/map-surface.js';

test('uses topology without a development key', () => {
  assert.equal(chooseMapMode({ key: '', online: true }), 'topology');
});

test('allows an explicit topology override for offline rehearsal', () => {
  assert.equal(chooseMapMode({ key: 'dev-key', online: true, forceTopology: true }), 'topology');
});

test('selects Tencent only when key and network are both available', () => {
  assert.equal(chooseMapMode({ key: 'dev-key', online: true }), 'tencent');
});

test('projects every station into finite SVG coordinates', () => {
  const points = projectStations(
    [
      { id: 'S1', longitude: 116.31, latitude: 39.91 },
      { id: 'S2', longitude: 116.47, latitude: 39.82 },
    ],
    { width: 1000, height: 600, padding: 80 }
  );
  assert.equal(points.length, 2);
  assert.ok(points.every(({ x, y }) => Number.isFinite(x) && Number.isFinite(y)));
});

test('centers a single station without producing NaN', () => {
  const points = projectStations([{ id: 'S1', longitude: 123.4395, latitude: 41.7331 }], {
    width: 1000,
    height: 600,
    padding: 80,
  });
  assert.equal(points.length, 1);
  assert.ok(Number.isFinite(points[0].x) && Number.isFinite(points[0].y));
});

test('falls back when the online renderer rejects', async () => {
  const calls = [];
  const surface = new MapSurface({
    onlineRenderer: { mount: async () => { throw new Error('blocked'); } },
    topologyRenderer: { mount: async () => calls.push('topology') },
    timeoutMs: 20,
  });
  const result = await surface.mount({}, { stations: [], key: 'dev-key', online: true });
  assert.equal(result.mode, 'topology');
  assert.deepEqual(calls, ['topology']);
});

test('late online completion cannot overwrite topology fallback', async () => {
  const container = { innerHTML: '' };
  let finishOnline;
  const onlineRenderer = {
    mount: (_container, { signal }) => new Promise((resolve, reject) => {
      finishOnline = () => (signal.aborted ? reject(new Error('aborted')) : resolve());
    }),
    destroy() {},
  };
  const topologyRenderer = {
    async mount(target) {
      target.innerHTML = 'topology';
    },
  };
  const surface = new MapSurface({ onlineRenderer, topologyRenderer, timeoutMs: 5 });
  const result = await surface.mount(container, { key: 'dev-key', online: true });
  finishOnline();
  await new Promise((resolve) => setTimeout(resolve, 0));
  assert.equal(result.mode, 'topology');
  assert.equal(container.innerHTML, 'topology');
});

test('clearFocus delegates to the active renderer', async () => {
  let cleared = false;
  const topologyRenderer = {
    async mount() {},
    clearFocus() {
      cleared = true;
    },
  };
  const surface = new MapSurface({ onlineRenderer: {}, topologyRenderer });
  await surface.mount({}, { key: '', online: false });
  await surface.clearFocus();
  assert.equal(cleared, true);
});

test('mount forwards onStationActivate in topology mode', async () => {
  let received;
  const topologyRenderer = {
    async mount(_container, { onStationActivate }) {
      received = onStationActivate;
    },
  };
  const surface = new MapSurface({ onlineRenderer: {}, topologyRenderer });
  await surface.mount({}, { key: '', online: false, onStationActivate: (id) => id });
  assert.equal(typeof received, 'function');
});

test('mount forwards onStationActivate alongside the abort signal in online mode', async () => {
  let received;
  const onlineRenderer = {
    async mount(_container, { onStationActivate, signal }) {
      assert.ok(signal instanceof AbortSignal);
      received = onStationActivate;
    },
    destroy() {},
  };
  const surface = new MapSurface({
    onlineRenderer,
    topologyRenderer: { async mount() {} },
    timeoutMs: 100,
  });
  const result = await surface.mount({}, {
    key: 'dev-key',
    online: true,
    onStationActivate: () => {},
  });
  assert.equal(result.mode, 'tencent');
  assert.equal(typeof received, 'function');
});

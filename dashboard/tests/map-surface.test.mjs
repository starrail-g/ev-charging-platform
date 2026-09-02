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

// 站点下拉全集目录契约测试：
// 分享链接首开（?stationId=…）/ 筛选后刷新 / 过滤响应不得覆盖全集 / 并发去重 / 失败可重试。
import test from 'node:test';
import assert from 'node:assert/strict';

import { createStationCatalog } from '../js/station-catalog.js';

const FULL = [
  { id: 1, name: '深圳演示一号站' },
  { id: 101, name: '分析站点01' },
  { id: 102, name: '分析站点02' },
];
const SINGLE = [{ id: 101, name: '分析站点01' }];

function okResult(stations) {
  return { ok: true, source: 'api', data: { stations }, fetchedAt: '2026-09-15T00:00:00Z' };
}

test('share link / post-filter refresh (initial station filter) still loads the full catalog', async () => {
  const calls = [];
  const published = [];
  const catalog = createStationCatalog({
    load: async (query) => {
      calls.push(query);
      return okResult(FULL);
    },
    onCatalog: (stations) => published.push(stations),
  });
  // 首帧即为过滤态：响应只含单站，不得成为下拉全集
  catalog.absorb(SINGLE, { filtered: true });
  assert.equal(catalog.stations, null, 'filtered response must not become the catalog');

  const stations = await catalog.ensure();
  assert.deepEqual(calls, [{}], 'catalog request must be unfiltered (no stationId / window)');
  assert.deepEqual(stations, FULL);
  assert.deepEqual(catalog.stations, FULL);
  assert.equal(published.length, 1, 'dropdown must be republished exactly once');
  assert.deepEqual(published[0], FULL);
});

test('normal unfiltered first load seals the catalog without an extra request', async () => {
  let calls = 0;
  const catalog = createStationCatalog({
    load: async () => {
      calls += 1;
      return okResult(FULL);
    },
  });
  catalog.absorb(FULL, { filtered: false });
  const stations = await catalog.ensure();
  assert.equal(calls, 0, 'cache hit must not issue a request');
  assert.deepEqual(stations, FULL);
});

test('filtered or degenerate responses never overwrite an existing catalog', () => {
  const catalog = createStationCatalog({ load: async () => okResult(FULL) });
  catalog.absorb(FULL, { filtered: false });
  catalog.absorb(SINGLE, { filtered: true });
  assert.deepEqual(catalog.stations, FULL, 'filtered response must keep the full catalog');
  catalog.absorb([], { filtered: false });
  assert.deepEqual(catalog.stations, FULL, 'empty unfiltered response must not clear the cache');
});

test('concurrent ensure calls dedupe into a single request', async () => {
  let calls = 0;
  let release;
  const gate = new Promise((resolve) => {
    release = resolve;
  });
  const catalog = createStationCatalog({
    load: async () => {
      calls += 1;
      await gate;
      return okResult(FULL);
    },
  });
  const first = catalog.ensure();
  const second = catalog.ensure();
  release();
  const [a, b] = await Promise.all([first, second]);
  assert.equal(calls, 1);
  assert.deepEqual(a, FULL);
  assert.deepEqual(b, FULL);
});

test('catalog failure stays unblocked and a later ensure retries', async () => {
  let calls = 0;
  const catalog = createStationCatalog({
    load: async () => {
      calls += 1;
      if (calls === 1) throw new Error('network down');
      return okResult(FULL);
    },
  });
  assert.equal(await catalog.ensure(), null, 'failed attempt resolves to current cache (null)');
  assert.equal(catalog.stations, null);
  const stations = await catalog.ensure();
  assert.equal(calls, 2, 'retry issues a fresh request');
  assert.deepEqual(stations, FULL);
});

test('superseded catalog result never publishes and the next ensure retries', async () => {
  let calls = 0;
  const catalog = createStationCatalog({
    load: async () => {
      calls += 1;
      if (calls === 1) return { ok: false, superseded: true, source: 'api' };
      return okResult(FULL);
    },
  });
  assert.equal(await catalog.ensure(), null);
  assert.equal(catalog.stations, null);
  assert.deepEqual(await catalog.ensure(), FULL);
});

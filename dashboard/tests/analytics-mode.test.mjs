// 分析链路契约测试（第二阶段）：
// 1) adaptAnalyticsPayload —— /api/dashboard 响应校验与视图模型映射；
// 2) ApiDataProvider —— 请求构造、错误码透传、迟到响应（superseded）防护、空窗口。
import test from 'node:test';
import assert from 'node:assert/strict';

import { adaptAnalyticsPayload } from '../js/data-adapter.js';
import { ApiDataProvider, ApiDataError } from '../js/api-data-provider.js';

function payload(overrides = {}) {
  return {
    status: 'ok',
    meta: {
      batch_id: 's2-smoke-20260915',
      source_type: 'synthetic_warehouse',
      timezone: 'UTC',
      coverage: 'complete',
      data_start: '2026-09-01T00:00:00Z',
      data_end_exclusive: '2026-09-02T00:00:00Z',
      generated_at: '2026-09-15T01:00:00Z',
      batch_generated_at: '2026-09-15T00:50:00Z',
      is_realtime: false,
      stale: false,
    },
    query: { start: '2026-09-01', end: '2026-09-02', station_id: null },
    data: {
      overview: { revenueCents: 1901, completedOrders: 2, energyWh: 15001, energyKwh: 15.001 },
      stations: [
        { id: 1, name: '测试充电站', address: '测试路1号', latitude: 22.5, longitude: 113.9,
          status: 'active', pileCount: 2, pileCounts: { idle: 1, reserved: 0, charging: 0, fault: 1, offline: 0 } },
      ],
      piles: [
        { id: 1, stationId: 1, code: 'P-1-A', type: 'fast', powerKw: 100.0,
          unitPriceCentsPerKwh: 120, status: 'idle', totalChargeCount: 1, totalChargeSeconds: 3600 },
        { id: 2, stationId: 1, code: 'P-1-B', type: 'fast', powerKw: 100.0,
          unitPriceCentsPerKwh: 120, status: 'fault', totalChargeCount: 1, totalChargeSeconds: 1800 },
      ],
      stationUtilization: [{ stationId: 1, utilization: 0.03125 }],
      stationRank: [{ stationId: 1, rank: 1, revenueCents: 1901, utilization: 0.03125,
                      completedOrders: 2, energyWh: 15001 }],
      revenueDaily: [{ date: '2026-09-01', stationId: 1, revenueCents: 1901,
                       completedOrders: 2, energyWh: 15001 }],
      loadHourly: [
        { hourStart: '2026-09-01T00:00:00Z', stationId: 1, allocatedWh: 5000.0, loadKw: 5.0,
          chargeSeconds: 1800, capacityPileSeconds: 7200 },
        { hourStart: '2026-09-01T01:00:00Z', stationId: 1, allocatedWh: 5000.0, loadKw: 5.0,
          chargeSeconds: 1800, capacityPileSeconds: 7200 },
      ],
      quality: { input: 7, kept: 3, duplicate: 1, quarantine: 3, repaired: 0 },
    },
    ...overrides,
  };
}

test('adapts analytics payload into the shared view model', () => {
  const model = adaptAnalyticsPayload(payload());
  assert.equal(model.isDemo, false);
  assert.equal(model.batchId, 's2-smoke-20260915');
  assert.equal(model.sourceType, 'synthetic_warehouse');
  assert.equal(model.updatedAt, '2026-09-15T00:50:00Z');
  assert.equal(model.coverage.start, '2026-09-01');
  assert.equal(model.coverage.endExclusive, '2026-09-02');
  assert.equal(model.coverage.defaultStart, '2026-09-01');       // 旧批次回退覆盖窗口
  assert.equal(model.coverage.defaultEndExclusive, '2026-09-02');
  assert.equal(model.metrics.totalPiles, 2);
  assert.equal(model.metrics.attentionCount, 1);
  assert.equal(model.metrics.availabilityPercent, 50);
  assert.equal(model.metrics.revenueCents, 1901);
  assert.equal(model.stationUtilization[0].name, '测试充电站');
  assert.equal(model.stationUtilization[0].utilization, 0.03125);
  assert.deepEqual(model.revenue7dCents, [1901]);
  assert.deepEqual(model.revenueSeriesLabels, ['9/1']);
  assert.equal(model.loadSeries.points[0].label, '9/1 00:00');
  assert.equal(model.loadSeries.points[1].loadKw, 5.0);
  assert.equal(model.empty, false);
});

test('published coverage window (available_*) wins over the generation window', () => {
  // 评审 P2：发布覆盖窗口 = 生成窗口 ∪ 末端结算追加日；前端筛选以可查询窗口为准，
  // 旧批次缺 available_* 时回退生成窗口（上一用例覆盖回退分支）。
  const withWindow = payload();
  withWindow.meta.available_start = '2026-09-01T00:00:00Z';
  withWindow.meta.available_end_exclusive = '2026-09-04T00:00:00Z';
  const model = adaptAnalyticsPayload(withWindow);
  assert.deepEqual(model.coverage, {
    start: '2026-09-01', endExclusive: '2026-09-04',
    defaultStart: '2026-09-01', defaultEndExclusive: '2026-09-04',
  });
});

test('default window (default_*) drives init/reset when coverage exceeds the limit', () => {
  // 覆盖 91 天时服务端把默认窗口收敛到最新 ≤90 天：前端“重置”必须回到 default_*，
  // 而不是会触发 window_too_large 的整段覆盖窗口。
  const payloadWithDefaults = payload();
  payloadWithDefaults.meta.available_start = '2026-06-16T00:00:00Z';
  payloadWithDefaults.meta.available_end_exclusive = '2026-09-15T00:00:00Z';
  payloadWithDefaults.meta.default_start = '2026-06-17';
  payloadWithDefaults.meta.default_end_exclusive = '2026-09-15';
  const model = adaptAnalyticsPayload(payloadWithDefaults);
  assert.equal(model.coverage.start, '2026-06-16');
  assert.equal(model.coverage.endExclusive, '2026-09-15');
  assert.equal(model.coverage.defaultStart, '2026-06-17');
  assert.equal(model.coverage.defaultEndExclusive, '2026-09-15');
});

test('empty status maps to an empty model without throwing', () => {
  const model = adaptAnalyticsPayload(payload({ status: 'empty' }));
  assert.equal(model.empty, true);
});

test('rejects payloads missing batch identity or smuggling demo data', () => {
  assert.throws(() => adaptAnalyticsPayload(payload({ meta: { ...payload().meta, batch_id: '' } })));
  assert.throws(() => adaptAnalyticsPayload(
    payload({ meta: { ...payload().meta, source_type: 'demo_fixture' } })));
  assert.throws(() => adaptAnalyticsPayload(payload({ status: 'error' })));
});

test('rejects non-integer cents and dangling pile references', () => {
  const bad = payload();
  bad.data.overview.revenueCents = 1901.5;
  assert.throws(() => adaptAnalyticsPayload(bad), /integer/);

  const dangling = payload();
  dangling.data.piles[0].stationId = 999;
  assert.throws(() => adaptAnalyticsPayload(dangling), /unknown stationId/);
});

function deferred() {
  let resolve;
  const promise = new Promise((res) => { resolve = res; });
  return { promise, resolve };
}

test('builds api url with window and station filters only', () => {
  const provider = new ApiDataProvider({ baseUrl: '.' });
  assert.equal(provider.buildUrl({}), './api/dashboard');
  assert.equal(provider.buildUrl({ start: '2026-09-01', end: '2026-09-02' }),
    './api/dashboard?start=2026-09-01&end=2026-09-02');
  assert.equal(provider.buildUrl({ start: '2026-09-01', end: '2026-09-02', stationId: 3 }),
    './api/dashboard?start=2026-09-01&end=2026-09-02&station_id=3');
  assert.equal(provider.buildUrl({ stationId: null }), './api/dashboard');
});

test('returns adapted model on success', async () => {
  const calls = [];
  const provider = new ApiDataProvider({
    baseUrl: '',
    fetchImpl: async (url) => {
      calls.push(url);
      return { ok: true, status: 200, json: async () => payload() };
    },
  });
  const result = await provider.load({ start: '2026-09-01', end: '2026-09-02' });
  assert.equal(result.ok, true);
  assert.equal(result.batchId, 's2-smoke-20260915');
  assert.equal(result.data.metrics.revenueCents, 1901);
  assert.match(calls[0], /start=2026-09-01/);
});

test('surfaces structured api error codes', async () => {
  const provider = new ApiDataProvider({
    baseUrl: '',
    fetchImpl: async () => ({
      ok: false, status: 503,
      json: async () => ({ status: 'error', error: { code: 'snapshot_missing', message: 'no snapshot' } }),
    }),
  });
  const result = await provider.load({});
  assert.equal(result.ok, false);
  assert.ok(result.error instanceof ApiDataError);
  assert.equal(result.error.code, 'snapshot_missing');
  assert.equal(result.error.httpStatus, 503);
});

test('non-json response is an explicit failure, never silent', async () => {
  const provider = new ApiDataProvider({
    baseUrl: '',
    fetchImpl: async () => ({ ok: true, status: 200, json: async () => { throw new Error('bad json'); } }),
  });
  const result = await provider.load({});
  assert.equal(result.ok, false);
  assert.equal(result.error.name, 'ApiDataError');
});

test('late response from an old filter never wins (superseded)', async () => {
  const first = deferred();
  const second = deferred();
  const queue = [first, second];
  const provider = new ApiDataProvider({
    baseUrl: '',
    fetchImpl: () => queue.shift().promise,
  });

  const slow = provider.load({ start: '2026-09-01' });
  const fast = provider.load({ start: '2026-09-02' });

  second.resolve({ ok: true, status: 200, json: async () => payload() });
  const fastResult = await fast;
  assert.equal(fastResult.ok, true);

  first.resolve({ ok: true, status: 200, json: async () => payload() });
  const slowResult = await slow;
  assert.equal(slowResult.superseded, true);
  assert.equal(slowResult.ok, false);
});

test('aborted in-flight request resolves as superseded, not error', async () => {
  const provider = new ApiDataProvider({
    baseUrl: '',
    fetchImpl: (url, opts) => new Promise((resolve, reject) => {
      if (opts?.signal) {
        opts.signal.addEventListener('abort', () => {
          const err = new Error('aborted');
          err.name = 'AbortError';
          reject(err);
        });
      }
    }),
  });
  const first = provider.load({ start: '2026-09-01' }); // 挂起，随后被新请求 abort
  const second = provider.load({ start: '2026-09-02' }); // 触发 abort，自身保持挂起
  const firstResult = await first;
  assert.equal(firstResult.ok, false);
  assert.equal(firstResult.superseded, true);
  void second; // 不等待：本测试只断言被 abort 的旧请求不产生错误态
});

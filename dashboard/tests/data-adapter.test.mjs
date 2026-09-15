import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { adaptAnalyticsPayload, adaptDashboardData, formatCents, mapPileStatus } from '../js/data-adapter.js';

const fixture = JSON.parse(await readFile(new URL('../data/demo.json', import.meta.url), 'utf8'));

test('formats integer cents without floating point drift', () => {
  assert.equal(formatCents(286540), '¥2,865.40');
});

test('maps every protocol state and unknown safely', () => {
  for (const value of ['idle', 'reserved', 'charging', 'fault', 'offline']) {
    assert.equal(mapPileStatus(value).protocol, value);
  }
  assert.equal(mapPileStatus('future-state').label, '未知');
});

test('derives overview metrics from six traceable piles', () => {
  const model = adaptDashboardData(fixture);
  assert.equal(model.metrics.totalPiles, 6);
  assert.equal(model.metrics.onlinePiles, 4);
  assert.equal(model.metrics.attentionCount, 2);
  assert.equal(model.metrics.availabilityPercent, 66.7);
  assert.equal(model.metrics.revenueCents, 286540);
  assert.equal(model.isDemo, true);
});

test('keeps 30-day revenue series consistent with the 7-day window (A-02)', () => {
  const model = adaptDashboardData(fixture);
  assert.equal(model.revenue30dCents.length, 30);
  for (const cents of model.revenue30dCents) {
    assert.ok(Number.isInteger(cents) && cents > 0, `30d series item must be positive cents: ${cents}`);
  }
  // 末 7 日与 7 日窗口同源，合计与 Qt mockdataset revenue30dCents 一致（983840 分）
  assert.deepEqual(
    model.revenue30dCents.slice(-7),
    model.revenue7dCents,
    'last 7 days of the 30-day series must equal the 7-day series',
  );
  assert.equal(
    model.revenue30dCents.reduce((sum, cents) => sum + cents, 0),
    983840,
    '30-day total must match the Qt mockdataset overview value',
  );
  assert.equal(model.revenue7dCents.reduce((sum, cents) => sum + cents, 0), 286540);
});

test('builds UTC day labels ending at the snapshot date', async () => {
  const { buildDayLabels } = await import('../js/charts.js');
  assert.deepEqual(
    buildDayLabels('2026-09-01T10:15:00Z', 7),
    ['8/26', '8/27', '8/28', '8/29', '8/30', '8/31', '9/1'],
  );
  assert.deepEqual(
    buildDayLabels('2026-09-01T10:15:00Z', 30).slice(0, 3),
    ['8/3', '8/4', '8/5'],
  );
  assert.equal(buildDayLabels('2026-09-01T10:15:00Z', 30).length, 30);
  assert.deepEqual(buildDayLabels('not-a-date', 7), []);
});

// 分析链路（analytics 模式）：营收序列必须按 7/30 日档位各自截取，不得把整窗塞进 30 日档。
test('slices analytics revenue series into the 7/30-day toggle windows', () => {
  const dates = Array.from({ length: 45 }, (_, index) => {
    const day = new Date(Date.UTC(2026, 7, 1) + index * 86400000);
    return day.toISOString().slice(0, 10);
  });
  const payload = {
    status: 'ok',
    meta: { batch_id: 'batch-1', source_type: 'generated_v0.4', generated_at: '2026-09-15T00:00:00Z',
            data_start: dates[0], data_end_exclusive: '2026-09-15' },
    data: {
      overview: { revenueCents: 100, completedOrders: 1, energyWh: 1000 },
      stations: [{ id: 101, name: '站点 101', latitude: 22.5, longitude: 113.9 }],
      piles: [{ id: 10101, stationId: 101, code: 'P-1', status: 'idle' }],
      stationUtilization: [{ stationId: 101, utilization: 0.1 }],
      revenueDaily: dates.map((date, index) => ({ date, revenueCents: 1000 + index,
                                                  completedOrders: 1, energyWh: 500 })),
      loadHourly: [{ hourStart: '2026-09-14T23:00:00Z', stationId: 101, loadKw: 1.5,
                     allocatedWh: 1500, chargeSeconds: 600 }],
    },
  };
  const model = adaptAnalyticsPayload(payload);
  assert.equal(model.revenueDaily.length, 45);
  assert.equal(model.revenue7dCents.length, 7);
  assert.deepEqual(model.revenue7dCents, model.revenueDaily.slice(-7).map((row) => row.cents));
  assert.equal(model.revenue30dCents.length, 30);
  assert.deepEqual(model.revenue30dCents, model.revenueDaily.slice(-30).map((row) => row.cents));
  assert.equal(model.revenueSeriesLabels.length, 45);
  assert.equal(model.revenueSeriesLabels.at(-1), '9/14');
});

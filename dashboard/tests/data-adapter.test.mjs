import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { adaptDashboardData, formatCents, mapPileStatus } from '../js/data-adapter.js';

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

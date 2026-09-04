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

import test from 'node:test';
import assert from 'node:assert/strict';
import { metric, panelMissing } from '../js/workbench-state.js';

test('absent metrics are distinct from observed zero', () => {
  for (const missing of [undefined, null, NaN, Infinity]) assert.equal(metric(missing), '未提供');
  assert.equal(metric(0, '%', 0, 100), '0%');
  assert.equal(metric(0.25, '%', 0, 100), '25%');
});
test('unpublished mining panels are unavailable even when equipment exists', () => {
  const analytics = { equipment: { status_counts: { idle: 67 } }, users: { total: 0 } };
  assert.equal(panelMissing('equipment', analytics), null);
  assert.equal(panelMissing('users', analytics), null);
  assert.ok(panelMissing('equipment-risk', analytics));
  assert.ok(panelMissing('user-cohort', analytics));
  assert.ok(panelMissing('revenue-regression', analytics));
  assert.equal(panelMissing('overview-health', { orders: { completion_rate: 0 } }), null);
});

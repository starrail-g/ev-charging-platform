import test from 'node:test';
import assert from 'node:assert/strict';
import { createDashboardState, deriveOverviewCards, timestampOf } from '../js/dashboard-state.js';

test('starts in loading and reaches content with freshness metadata', () => {
  const state = createDashboardState().resolve({ stations: [{ id: 'S1' }], fetchedAt: '2026-09-01T10:15:00Z' });
  assert.equal(state.kind, 'content');
  assert.equal(state.isStale, false);
});

test('never marks stale from an invalid fetched timestamp', () => {
  assert.equal(timestampOf('not-a-date'), Number.NaN);
  assert.equal(timestampOf(null), Number.NaN);
  const state = createDashboardState({ liveMode: true, staleAfterMs: 1 });
  const result = state.resolve({ stations: [{ id: 'S1' }], fetchedAt: 'garbage' });
  assert.equal(result.kind, 'content');
  assert.equal(result.isStale, false);
});

test('keeps empty, error, offline and unavailable semantics distinct', () => {
  const state = createDashboardState();
  assert.equal(state.empty().kind, 'empty');
  assert.equal(state.reject('network').kind, 'error');
  assert.equal(state.offline().kind, 'offline');
  assert.equal(state.modelUnavailable().kind, 'model-unavailable');
});

test('overview cards use traceable model values', () => {
  const cards = deriveOverviewCards({
    totalPiles: 6,
    onlinePiles: 4,
    availabilityPercent: 66.7,
    attentionCount: 2,
    revenueCents: 286540,
    avgStationUtilization: 0.42,
  });
  assert.deepEqual(cards.map((card) => card.value), ['66.7%', '4', '42%', '¥2,865.40']);
  assert.deepEqual(cards.map((card) => card.label), ['网络可用率', '在线充电桩', '平均站点利用率', '近 7 日营收']);
});

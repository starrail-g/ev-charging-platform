import test from 'node:test';
import assert from 'node:assert/strict';
import { createDashboardState } from '../js/dashboard-state.js';
import { statePresentation } from '../js/state-view.js';

test('every operational state exposes title, description and icon', () => {
  const state = createDashboardState();
  const samples = [
    state.loading(),
    state.resolve({ stations: [{ id: 'S1' }], fetchedAt: '2026-09-01T10:15:00Z' }),
    state.empty(),
    state.reject('boom'),
    state.offline(),
    state.stale('2026-09-01T10:15:00Z'),
    state.modelUnavailable(),
  ];
  for (const sample of samples) {
    const presentation = statePresentation(sample);
    assert.ok(presentation.title, `title for ${sample.kind}`);
    assert.ok(presentation.description, `description for ${sample.kind}`);
    assert.ok(presentation.icon, `icon for ${sample.kind}`);
  }
});

test('error and stale expose actions while model unavailable does not', () => {
  const state = createDashboardState();
  assert.equal(statePresentation(state.reject('x')).action, '重试');
  assert.equal(statePresentation(state.stale('2026-09-01T10:15:00Z')).action, '刷新');
  assert.equal(statePresentation(state.modelUnavailable()).action, null);
  assert.equal(statePresentation(state.empty()).action, '重试');
  assert.equal(statePresentation(state.resolve({ stations: [{ id: 'S1' }] })).action, null);
});

test('error presentation carries the concrete failure detail', () => {
  const state = createDashboardState();
  const presentation = statePresentation(state.reject('demo.json 加载失败'));
  assert.ok(presentation.description.includes('demo.json 加载失败'));
});

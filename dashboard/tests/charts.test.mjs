import test from 'node:test';
import assert from 'node:assert/strict';
import { buildChartPalette } from '../js/charts.js';

test('chart palette contains resolved colors rather than CSS var expressions', () => {
  const values = {
    '--night-text': '#E9F5ED',
    '--night-muted-text': '#A6B0B4',
    '--night-decorative': '#2A4554',
    '--night-surface': '#0F1E26',
  };
  const palette = buildChartPalette((name) => values[name]);
  assert.deepEqual(palette, {
    text: '#E9F5ED', muted: '#A6B0B4', divider: '#2A4554', surface: '#0F1E26',
  });
  assert.ok(Object.values(palette).every((value) => !value.startsWith('var(')));
});

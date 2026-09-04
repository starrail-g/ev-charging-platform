import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

const css = await readFile(new URL('../css/app.css', import.meta.url), 'utf8');
const html = await readFile(new URL('../index.html', import.meta.url), 'utf8');

test('aurora animation closes its cycle with a natural pause', () => {
  assert.match(css, /@keyframes\s+aurora-breathe[\s\S]*0%[\s\S]*45%[\s\S]*90%[\s\S]*100%/);
  assert.match(css, /animation-duration:\s*11s/);
});

test('honors reduced motion and contains responsive breakpoints', () => {
  assert.match(css, /prefers-reduced-motion:\s*reduce/);
  assert.match(css, /max-width:\s*1500px/);
  assert.match(css, /max-width:\s*900px/);
});

test('does not restore the rejected center motion ring', () => {
  assert.doesNotMatch(html + css, /center-motion-ring|radar-sweep-ring/);
});

test('compact layout places metrics above map and right rail', () => {
  const start = css.indexOf('@media (max-width: 1500px) and (min-width: 901px)');
  const end = css.indexOf('@media (max-width: 900px)', start);
  assert.ok(start >= 0 && end > start, 'compact media block must precede mobile block');
  const compact = css.slice(start, end);
  assert.match(compact, /\.metric-strip\s*\{[\s\S]*grid-column:\s*1\s*\/\s*-1/);
  assert.match(compact, /\.map-wrap\s*\{[\s\S]*grid-column:\s*1/);
  assert.match(compact, /\.right-rail\s*\{[\s\S]*grid-column:\s*2/);
});

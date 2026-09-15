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

test('analysis workbench exposes all second-stage domains and ML prediction entry', () => {
  assert.match(html, /二阶段大数据分析工作台/);
  for (const label of ['机器学习预测', '充电用户', '充电设备', '充电订单', '充电能源', '运营收益', '站点运营', '评价与服务']) {
    assert.match(html, new RegExp(label));
  }
  for (const id of ['analysis-forecast', 'analysis-users', 'analysis-equipment', 'analysis-orders', 'analysis-energy', 'analysis-revenue', 'analysis-stations', 'analysis-service']) {
    assert.match(html, new RegExp(`id="${id}"`));
  }
});

test('second-stage workbench uses independent navigable pages', () => {
  assert.match(html, /class="workspace-layout"/);
  for (const page of ['overview', 'forecast', 'audience', 'operations', 'business', 'service']) {
    assert.match(html, new RegExp(`data-workspace-page="${page}"`));
  }
  assert.match(html, /data-workspace-page="forecast"[\s\S]*智能预测/);
  assert.match(html, /data-workspace-page="audience"[\s\S]*用户与设备/);
  assert.match(css, /\.workspace-layout\s*\{[\s\S]*grid-template-columns:\s*224px/);
  assert.match(css, /@media \(max-width: 1180px\)[\s\S]*workspace-nav-list[\s\S]*overflow-x:\s*auto/);
});

test('every workspace page exposes at least four mining modules', () => {
  const groups = ['forecast', 'audience', 'operations', 'business', 'service'];
  for (const group of groups) {
    const count = [...html.matchAll(new RegExp(`data-analysis-group="${group}"`, 'g'))].length;
    assert.ok(count >= 4, `${group} page should expose at least four modules (got ${count})`);
  }
  assert.equal((html.match(/class="insight-card"/g) ?? []).length, 4, 'overview should expose four diagnostic cards');
  for (const method of ['模型回归', 'RFM', 'z-score', '漏斗', '时序', 'OLS', 'Pareto', '控制图']) {
    assert.match(html, new RegExp(method));
  }
});

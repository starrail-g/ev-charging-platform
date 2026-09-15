// 分析模式 ML 区来源标注契约（PR #24 评审 P2）：
// analytics 模式的 ML 区来自独立服务链路（另一批次/站点编号/窗口），必须显式标注为
// 独立数据来源并说明不受当前筛选影响，不得让用户读成当前筛选结果。
import test from 'node:test';
import assert from 'node:assert/strict';

import {
  analysisSourceAttribution, ML_INDEPENDENT_CHIP, ML_INDEPENDENT_NOTE,
} from '../js/analysis-source.js';

test('analytics mode labels the ML area as an independent data source', () => {
  const attribution = analysisSourceAttribution({ ok: true, independent: true });
  assert.equal(attribution.chip, ML_INDEPENDENT_CHIP);
  assert.equal(attribution.state, 'independent');
  assert.match(attribution.note, /独立/);
  assert.match(attribution.note, /不受本页窗口\/站点筛选影响/);   // 明示不受当前筛选影响
  assert.match(attribution.metaPrefix, /独立数据源/);
  assert.equal(attribution.metaPrefix, '独立数据源（ML 服务链路）· ');
});

test('default (ML链) mode keeps the regular updated chip without source notes', () => {
  const attribution = analysisSourceAttribution({ ok: true, independent: false });
  assert.equal(attribution.chip, '已更新');
  assert.equal(attribution.state, 'ready');
  assert.equal(attribution.note, '');
  assert.equal(attribution.metaPrefix, '');
});

test('failed analysis stays degraded regardless of mode', () => {
  for (const independent of [true, false]) {
    const attribution = analysisSourceAttribution({ ok: false, independent });
    assert.equal(attribution.state, 'degraded');
    assert.equal(attribution.note, '');
  }
});

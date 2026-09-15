// ML 分析区的来源标注（PR #24 评审 P2）。
//
// analytics 模式（?source=analytics）下，右栏“智能预测与调度”与预测工作台来自
// 独立的 ML 服务链路：与当前批次是不同的数据生成（批次、站点编号、时间窗口均无
// 对应关系），也不受本页窗口/站点筛选影响。界面必须显式标注为独立数据来源，
// 不得让用户把它读成当前筛选结果（或与批次数据同源）。

export const ML_INDEPENDENT_CHIP = '独立 ML 链路';

export const ML_INDEPENDENT_NOTE =
  'ML 预测/推荐来自独立分析服务链路（独立批次，站点编号与当前批次无对应关系），'
  + '不受本页窗口/站点筛选影响。';

export const ML_INDEPENDENT_META_PREFIX = '独立数据源（ML 服务链路）· ';

/**
 * 分析区来源标注：{ chip, state, note, metaPrefix }。
 * - 失败态（ok=false）由调用方按错误码决定 chip 文案，这里只给降级态；
 * - analytics 模式（independent=true）→ 独立来源标注；
 * - 默认/ML 模式 → 常规“已更新”，无附注。
 */
export function analysisSourceAttribution({ ok, independent } = {}) {
  if (!ok) {
    return { chip: null, state: 'degraded', note: '', metaPrefix: '' };
  }
  if (independent) {
    return {
      chip: ML_INDEPENDENT_CHIP,
      state: 'independent',
      note: ML_INDEPENDENT_NOTE,
      metaPrefix: ML_INDEPENDENT_META_PREFIX,
    };
  }
  return { chip: '已更新', state: 'ready', note: '', metaPrefix: '' };
}

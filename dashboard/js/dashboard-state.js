// 页面级有限状态模型：loading / content / empty / error / offline / stale / model-unavailable。
// stale 由 fetchedAt 与阈值计算（实时模式），不能只靠颜色表达；每种状态都带
// 图标、标题、说明与可执行动作标签，供状态区与局部面板渲染。
import { formatCents } from './data-adapter.js';

const STATE_META = Object.freeze({
  loading: { icon: 'loader', title: '正在加载数据', description: '从本地演示数据读取中……', action: null },
  content: { icon: 'activity', title: '数据已就绪', description: '主屏展示最近一次加载的演示数据', action: null },
  empty: { icon: 'inbox', title: '暂无数据', description: '当前没有可展示的站点或充电桩', action: '重试' },
  error: { icon: 'alert-triangle', title: '数据加载失败', description: '无法读取演示数据，请检查本地服务后重试', action: '重试' },
  offline: { icon: 'wifi-off', title: '离线模式', description: '正在使用本地数据与离线拓扑图，核心演示不受影响', action: '重试' },
  stale: { icon: 'clock', title: '数据可能已过期', description: '超过刷新阈值仍未获得新数据', action: '刷新' },
  'model-unavailable': { icon: 'cpu', title: '智能预测与调度模型待接入', description: '当前为演示数据；模型接入后此处显示真实预测结果', action: null },
});

const DEFAULT_STALE_AFTER_MS = 5 * 60 * 1000; // 实时模式默认 5 分钟

/** 合法时间戳返回毫秒值，否则 NaN——stale 计算永不因脏数据崩溃。 */
export function timestampOf(value) {
  const time = value != null && value !== '' ? new Date(value).getTime() : NaN;
  return Number.isFinite(time) ? time : NaN;
}

export function createDashboardState({ liveMode = false, staleAfterMs = DEFAULT_STALE_AFTER_MS } = {}) {
  return {
    resolve(payload) {
      const { stations = [], fetchedAt = null } = payload;
      if (stations.length === 0) return this.empty();
      const fetchedAtMs = timestampOf(fetchedAt);
      const isStale = liveMode && Number.isFinite(fetchedAtMs)
        ? Date.now() - fetchedAtMs > staleAfterMs
        : false;
      const kind = isStale ? 'stale' : 'content';
      return { kind, isStale, ...STATE_META[kind], ...payload };
    },
    loading() {
      return { kind: 'loading', isStale: false, ...STATE_META.loading };
    },
    empty() {
      return { kind: 'empty', isStale: false, ...STATE_META.empty };
    },
    reject(message) {
      return { kind: 'error', isStale: false, ...STATE_META.error, detail: message ?? '' };
    },
    offline() {
      return { kind: 'offline', isStale: false, ...STATE_META.offline };
    },
    stale(fetchedAt) {
      return { kind: 'stale', isStale: true, fetchedAt, ...STATE_META.stale };
    },
    modelUnavailable() {
      return { kind: 'model-unavailable', isStale: false, ...STATE_META['model-unavailable'] };
    },
  };
}

/** 概览指标卡（顺序与 visual v4 左栏一致：可用率 / 在线桩 / 平均利用率 / 营收）。 */
export function deriveOverviewCards(metrics) {
  const utilizationPercent = Math.round((metrics.avgStationUtilization ?? 0) * 100);
  return [
    { key: 'availability', label: '网络可用率', value: `${metrics.availabilityPercent}%` },
    {
      key: 'online',
      label: '在线充电桩',
      value: String(metrics.onlinePiles ?? metrics.totalPiles),
      sub: `/ ${metrics.totalPiles} 台`,
    },
    { key: 'utilization', label: '平均站点利用率', value: `${utilizationPercent}%` },
    { key: 'revenue', label: '近 7 日营收', value: formatCents(metrics.revenueCents) },
  ];
}

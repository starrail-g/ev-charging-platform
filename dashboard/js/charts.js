// ECharts 图表工厂：仅三类图（24h 负荷折线 / 五态分布环图 / 7 日营收趋势）。
// 颜色读取 CSS 生成令牌（theme.css）后**解析为实际色值**再交给 ECharts——
// canvas 渲染不认识 var(--...)，直接传变量字符串会回退成黑色。
// 金额始终由整数分转换，标题标注演示数据。
import { mapPileStatus, STATUS_META } from './status-map.js';
import { formatCents } from './data-adapter.js';

const TEXT_FAMILY = 'system-ui, "Segoe UI", "Microsoft YaHei", sans-serif';

function resolveVar(name) {
  const style = getComputedStyle(document.documentElement);
  return (style.getPropertyValue(name) || '').trim() || '#4DD7FF';
}

/** 把 CSS 令牌解析为 ECharts canvas 可用的实际颜色。 */
export function buildChartPalette(read = resolveVar) {
  return {
    text: read('--night-text'),
    muted: read('--night-muted-text'),
    divider: read('--night-decorative'),
    surface: read('--night-surface'),
  };
}

/** 基础主题：只含 ECharts 顶层合法组件（textStyle/legend/tooltip）。 */
function chartTheme() {
  const palette = buildChartPalette();
  const textStyle = { color: palette.text, fontFamily: TEXT_FAMILY };
  return {
    palette,
    textStyle,
    legend: { textStyle },
    tooltip: {
      backgroundColor: palette.surface,
      borderColor: palette.divider,
      textStyle,
    },
  };
}

function getChart(el) {
  if (typeof echarts === 'undefined') {
    throw new Error('echarts script missing');
  }
  const chart = echarts.getInstanceByDom(el) ?? echarts.init(el);
  return chart;
}

/** 24 小时充电负荷折线。 */
export function renderLoadChart(el, model) {
  const points = model.demoSeries?.points ?? [];
  const chart = getChart(el);
  const theme = chartTheme();
  chart.setOption({
    ...theme,
    grid: { left: 48, right: 20, top: 28, bottom: 34 },
    tooltip: { ...theme.tooltip, trigger: 'axis' },
    xAxis: {
      type: 'category',
      data: points.map((point) => `${point.hour}:00`),
      axisLabel: { color: theme.palette.muted },
      axisLine: { lineStyle: { color: theme.palette.divider } },
    },
    yAxis: {
      type: 'value',
      name: 'kW',
      nameTextStyle: { color: theme.palette.muted },
      axisLabel: { color: theme.palette.muted },
      axisLine: { lineStyle: { color: theme.palette.divider } },
      splitLine: { lineStyle: { color: theme.palette.divider, opacity: 0.5 } },
    },
    series: [
      {
        type: 'line',
        data: points.map((point) => point.loadKw),
        smooth: true,
        symbol: 'none',
        lineStyle: { color: resolveVar('--state-charging'), width: 2 },
        areaStyle: {
          color: {
            type: 'linear',
            x: 0,
            y: 0,
            x2: 0,
            y2: 1,
            colorStops: [
              { offset: 0, color: 'rgba(77, 215, 255, 0.35)' },
              { offset: 1, color: 'rgba(77, 215, 255, 0.02)' },
            ],
          },
        },
      },
    ],
  });
  return chart;
}

/** 桩状态五态分布环图（含 unknown 兜底）。 */
export function renderStateDonut(el, counts) {
  const protocols = Object.keys(STATUS_META);
  const chart = getChart(el);
  const theme = chartTheme();
  const data = protocols
    .map((protocol) => ({
      name: STATUS_META[protocol].label,
      value: counts[protocol] ?? 0,
      itemStyle: { color: mapPileStatus(protocol).color },
    }))
    .filter((item) => item.value > 0);
  chart.setOption({
    ...theme,
    tooltip: { ...theme.tooltip, trigger: 'item', formatter: '{b}: {c} 台（{d}%）' },
    legend: { ...theme.legend, bottom: 0, icon: 'circle', itemWidth: 10, itemHeight: 10 },
    series: [
      {
        type: 'pie',
        radius: ['52%', '74%'],
        center: ['50%', '44%'],
        avoidLabelOverlap: true,
        itemStyle: { borderColor: resolveVar('--night-bg'), borderWidth: 2 },
        label: { color: theme.palette.text, formatter: '{b}\n{c} 台' },
        data,
      },
    ],
  });
  return chart;
}

/**
 * UTC 日标签序列（'M/d'）：以 endDateIso 所在 UTC 日为终点，向前 count-1 天。
 * 纯函数（无 DOM），供近 7 日/近 30 日营收图共用 x 轴。
 */
export function buildDayLabels(endDateIso, count) {
  const day = (endDateIso ?? '').slice(0, 10);
  const parts = day.split('-').map(Number);
  if (parts.length !== 3 || parts.some((v) => !Number.isInteger(v) || v <= 0)) {
    return [];
  }
  const endUtc = Date.UTC(parts[0], parts[1] - 1, parts[2]);
  const labels = [];
  for (let offset = count - 1; offset >= 0; offset -= 1) {
    const at = new Date(endUtc - offset * 86400000);
    labels.push(`${at.getUTCMonth() + 1}/${at.getUTCDate()}`);
  }
  return labels;
}

/** 营收趋势（金额由整数分转换；endDateIso 决定 x 轴日期终点）。 */
export function renderRevenueTrend(el, centsSeries, endDateIso) {
  const chart = getChart(el);
  const theme = chartTheme();
  chart.setOption({
    ...theme,
    grid: { left: 58, right: 16, top: 20, bottom: 30 },
    tooltip: {
      ...theme.tooltip,
      trigger: 'axis',
      valueFormatter: (value) => formatCents(value),
    },
    xAxis: {
      type: 'category',
      data: buildDayLabels(endDateIso, centsSeries.length),
      axisLabel: { color: theme.palette.muted },
      axisLine: { lineStyle: { color: theme.palette.divider } },
    },
    yAxis: {
      type: 'value',
      axisLabel: {
        color: theme.palette.muted,
        formatter: (value) => `¥${Math.round(value / 100)}`,
      },
      axisLine: { lineStyle: { color: theme.palette.divider } },
      splitLine: { lineStyle: { color: theme.palette.divider, opacity: 0.5 } },
    },
    series: [
      {
        type: 'bar',
        data: centsSeries,
        barWidth: '46%',
        itemStyle: {
          color: {
            type: 'linear',
            x: 0,
            y: 0,
            x2: 0,
            y2: 1,
            colorStops: [
              { offset: 0, color: resolveVar('--state-idle') },
              { offset: 1, color: 'rgba(183, 243, 106, 0.25)' },
            ],
          },
          borderRadius: [4, 4, 0, 0],
        },
        label: { show: false },
      },
    ],
  });
  return chart;
}

/** 统一注册容器尺寸变化（ResizeObserver 由调用方驱动）。 */
export function resizeAll(charts) {
  for (const chart of charts) chart?.resize();
}

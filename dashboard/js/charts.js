// ECharts 图表工厂：总览态势图与二阶段数据挖掘模块共用的可视化配置。
// 颜色读取 CSS 生成令牌（theme.css）后**解析为实际色值**再交给 ECharts——
// canvas 渲染不认识 var(--...)，直接传变量字符串会回退成黑色。
// 金额始终由整数分转换；正常运行数据来自 Schema v0.4 快照。
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
    aria: { show: true },
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

/** 24 小时充电负荷折线（demo 用 demoSeries；分析链路用 loadSeries，标签带完整日期）。 */
export function renderLoadChart(el, model) {
  const points = model.demoSeries?.points ?? model.loadSeries?.points ?? [];
  const chart = getChart(el);
  const theme = chartTheme();
  chart.setOption({
    ...theme,
    grid: { left: 48, right: 20, top: 28, bottom: 34 },
    tooltip: { ...theme.tooltip, trigger: 'axis' },
    xAxis: {
      type: 'category',
      data: points.map((point) =>
        point.hour !== undefined ? `${point.hour}:00` : (point.label ?? '')),
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

/**
 * 营收趋势（金额由整数分转换；endDateIso 决定 x 轴日期终点）。
 * labels 显式给出时优先使用（分析链路按实际日期序列; 天数不固定）。
 */
export function renderRevenueTrend(el, centsSeries, endDateIso, labels = null) {
  const chart = getChart(el);
  const theme = chartTheme();
  const axisLabels = Array.isArray(labels) && labels.length === centsSeries.length
    ? labels
    : buildDayLabels(endDateIso, centsSeries.length);
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
      data: axisLabels,
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

function renderAnalysisChart(el, option) {
  const chart = getChart(el);
  chart.setOption({ ...chartTheme(), ...option });
  return chart;
}

/** 用户分群：按完成订单次数观察新客与复购客结构。 */
export function renderUserSegments(el, segments = [], mining = {}) {
  const labels = segments.map((item) => item.label);
  return renderAnalysisChart(el, {
    tooltip: { ...chartTheme().tooltip, trigger: 'item' },
    legend: { ...chartTheme().legend, bottom: 0 },
    grid: { left: 42, right: 18, top: 16, bottom: 38 },
    xAxis: { type: 'category', data: labels, axisLabel: { color: chartTheme().palette.muted } },
    yAxis: { type: 'value', axisLabel: { color: chartTheme().palette.muted } },
    series: [{ type: 'bar', name: '用户数', data: segments.map((item) => item.count), itemStyle: { color: resolveVar('--night-focus'), borderRadius: [5, 5, 0, 0] }, label: { show: true, position: 'top' } }],
  });
}

/** 设备运行：状态分布与快慢充类型分布。 */
export function renderEquipmentOverview(el, equipment = {}, mining = {}) {
  const status = equipment.status_counts ?? {};
  const labels = Object.keys(status);
  const values = labels.map((key) => status[key]);
  return renderAnalysisChart(el, {
    tooltip: { ...chartTheme().tooltip, trigger: 'axis' },
    grid: { left: 42, right: 18, top: 22, bottom: 34 },
    xAxis: { type: 'category', data: labels, axisLabel: { color: chartTheme().palette.muted } },
    yAxis: { type: 'value', axisLabel: { color: chartTheme().palette.muted } },
    series: [{ type: 'bar', data: values, itemStyle: { color: resolveVar('--state-charging'), borderRadius: [4, 4, 0, 0] }, markPoint: { data: [{ type: 'max', name: '峰值状态' }] } }],
  });
}

/** 订单趋势：完成、取消和总量同图，支持观察转化质量。 */
export function renderOrderTrend(el, daily = []) {
  const labels = daily.map((row) => row.date?.slice(5) ?? '—');
  return renderAnalysisChart(el, {
    tooltip: { ...chartTheme().tooltip, trigger: 'axis' },
    legend: { ...chartTheme().legend, top: 0 },
    grid: { left: 42, right: 18, top: 36, bottom: 30 },
    xAxis: { type: 'category', data: labels, axisLabel: { color: chartTheme().palette.muted } },
    yAxis: [{ type: 'value', axisLabel: { color: chartTheme().palette.muted } }, { type: 'value', max: 100, axisLabel: { color: chartTheme().palette.muted, formatter: '{value}%' } }],
    series: [
      { name: '全部订单', type: 'line', smooth: true, data: daily.map((row) => row.total), lineStyle: { color: resolveVar('--night-focus') } },
      { name: '完成订单', type: 'line', smooth: true, data: daily.map((row) => row.completed), lineStyle: { color: resolveVar('--state-idle') } },
      { name: '取消订单', type: 'line', smooth: true, data: daily.map((row) => row.cancelled), lineStyle: { color: resolveVar('--state-fault') } },
      { name: '完成率', type: 'line', yAxisIndex: 1, smooth: true, data: daily.map((row) => row.total ? Math.round((row.completed / row.total) * 100) : 0), lineStyle: { color: resolveVar('--night-text'), type: 'dashed' } },
    ],
  });
}

/** 能源分析：小时能耗与订单量双轴。 */
export function renderEnergyProfile(el, hourly = []) {
  const labels = hourly.map((row) => `${row.hour}:00`);
  return renderAnalysisChart(el, {
    tooltip: { ...chartTheme().tooltip, trigger: 'axis' },
    legend: { ...chartTheme().legend, top: 0 },
    grid: { left: 48, right: 44, top: 36, bottom: 30 },
    xAxis: { type: 'category', data: labels, axisLabel: { color: chartTheme().palette.muted } },
    yAxis: [{ type: 'value', name: 'kWh', axisLabel: { color: chartTheme().palette.muted } }, { type: 'value', name: '单', axisLabel: { color: chartTheme().palette.muted } }],
    series: [
      { name: '能耗', type: 'bar', data: hourly.map((row) => row.energy_kwh), itemStyle: { color: resolveVar('--state-charging'), opacity: 0.78 } },
      { name: '订单量', type: 'line', yAxisIndex: 1, smooth: true, data: hourly.map((row) => row.order_count), lineStyle: { color: resolveVar('--state-idle') } },
    ],
  });
}

/** 站点经营：按营收排序的 Top 站点。 */
export function renderStationRanking(el, stations = []) {
  const rows = stations.slice(0, 10).reverse();
  return renderAnalysisChart(el, {
    tooltip: { ...chartTheme().tooltip, trigger: 'axis', valueFormatter: (value) => formatCents(value) },
    grid: { left: 84, right: 34, top: 12, bottom: 24 },
    xAxis: { type: 'value', axisLabel: { color: chartTheme().palette.muted, formatter: (value) => `¥${Math.round(value / 100)}` } },
    yAxis: { type: 'category', data: rows.map((row) => row.name), axisLabel: { color: chartTheme().palette.muted } },
    series: [{ type: 'bar', data: rows.map((row) => row.revenue_cents), itemStyle: { color: resolveVar('--state-idle'), borderRadius: [0, 4, 4, 0] } }],
  });
}

/** 服务质量代理：完成率、非取消率、复购率和履约时长。 */
export function renderServiceQuality(el, service = {}) {
  const values = [service.completion_rate ?? 0, 1 - (service.cancel_rate ?? 0), service.repeat_user_rate ?? 0].map((value) => Math.round(value * 100));
  return renderAnalysisChart(el, {
    tooltip: { ...chartTheme().tooltip, trigger: 'axis', valueFormatter: (value) => `${value}%` },
    grid: { left: 48, right: 18, top: 22, bottom: 34 },
    xAxis: { type: 'category', data: ['完成率', '非取消率', '复购率'], axisLabel: { color: chartTheme().palette.muted } },
    yAxis: { type: 'value', max: 100, axisLabel: { color: chartTheme().palette.muted, formatter: '{value}%' } },
    series: [{ type: 'bar', data: values, itemStyle: { color: resolveVar('--night-focus'), borderRadius: [4, 4, 0, 0] }, label: { show: true, position: 'top', formatter: '{c}%' } }],
  });
}

/** 机器学习预测：按预测窗口展示站点负荷，供答辩直接观察 1/6/24 小时结果。 */
export function renderForecastHorizon(el, items = []) {
  const rows = [...items].sort((a, b) => a.horizon_hours - b.horizon_hours);
  return renderAnalysisChart(el, {
    tooltip: { ...chartTheme().tooltip, trigger: 'axis', valueFormatter: (value) => `${Number(value).toFixed(1)} kWh` },
    grid: { left: 52, right: 18, top: 26, bottom: 34 },
    xAxis: { type: 'category', data: rows.map((row) => `${row.horizon_hours} 小时`), axisLabel: { color: chartTheme().palette.muted } },
    yAxis: { type: 'value', name: '预测负荷 kWh', axisLabel: { color: chartTheme().palette.muted } },
    series: [{ name: '预测负荷', type: 'bar', data: rows.map((row) => Number(row.predicted_value ?? 0)), itemStyle: { color: resolveVar('--night-focus'), borderRadius: [4, 4, 0, 0] }, label: { show: true, position: 'top', formatter: ({ value }) => Number(value).toFixed(1) } }],
  });
}

function emptySeries(label = '暂无数据') {
  return [{ name: label, type: 'bar', data: [0], itemStyle: { color: resolveVar('--night-decorative') }, label: { show: true, formatter: label } }];
}

/** 总览：把可用率、完成率、利用率和异常率合成为可解释健康分。 */
export function renderHealthScore(el, metrics = {}, analytics = {}) {
  const availability = Number(metrics.availabilityPercent ?? 0) / 100;
  const utilization = Number(metrics.avgStationUtilization ?? 0);
  const completion = Number(analytics.orders?.completion_rate ?? 0);
  const attention = Number(metrics.attentionCount ?? 0) / Math.max(1, Number(metrics.totalPiles ?? 0));
  const score = Math.round(Math.max(0, Math.min(1, availability * 0.35 + completion * 0.35 + Math.min(1, utilization / 0.7) * 0.2 + (1 - attention) * 0.1)) * 100);
  return renderAnalysisChart(el, {
    series: [{ type: 'gauge', startAngle: 210, endAngle: -30, min: 0, max: 100, radius: '88%', center: ['50%', '57%'], progress: { show: true, width: 13, itemStyle: { color: resolveVar('--night-focus') } }, axisLine: { lineStyle: { width: 13, color: [[1, resolveVar('--night-decorative')]] } }, pointer: { show: false }, axisTick: { show: false }, splitLine: { show: false }, axisLabel: { show: false }, detail: { offsetCenter: [0, '8%'], color: chartTheme().palette.text, fontSize: 24, formatter: '{value} 分' }, data: [{ value: score }] }],
    graphic: [{ type: 'text', left: 'center', top: '83%', style: { text: `可用率 ${Math.round(availability * 100)}% · 完成率 ${Math.round(completion * 100)}%`, fill: chartTheme().palette.muted, fontSize: 11 } }],
  });
}

/** 总览：展示最偏离累计充电次数基线的桩。 */
export function renderAnomalyScan(el, mining = {}) {
  const rows = (mining.top_anomalies ?? []).slice(0, 6).reverse();
  return renderAnalysisChart(el, {
    tooltip: { ...chartTheme().tooltip, trigger: 'axis', valueFormatter: (value) => `z=${Number(value).toFixed(2)}` },
    grid: { left: 84, right: 20, top: 12, bottom: 24 },
    xAxis: { type: 'value', name: 'z-score', axisLabel: { color: chartTheme().palette.muted } },
    yAxis: { type: 'category', data: rows.map((row) => row.pile_code ?? `桩 ${row.pile_id}`), axisLabel: { color: chartTheme().palette.muted } },
    series: rows.length ? [{ type: 'bar', data: rows.map((row) => Number(row.z_score ?? 0)), itemStyle: { color: resolveVar('--state-fault'), borderRadius: [0, 4, 4, 0] }, markLine: { data: [{ xAxis: Number(mining.threshold ?? 2) }, { xAxis: -Number(mining.threshold ?? 2) }] } }] : emptySeries('暂无异常桩'),
  });
}

/** 总览：站点高负荷/均衡/低负荷聚类的平均利用率。 */
export function renderClusterSummary(el, mining = {}) {
  const rows = mining.centroids ?? [];
  return renderAnalysisChart(el, {
    tooltip: { ...chartTheme().tooltip, trigger: 'axis', valueFormatter: (value) => `${Math.round(Number(value) * 100)}%` },
    grid: { left: 50, right: 18, top: 18, bottom: 32 },
    xAxis: { type: 'category', data: rows.map((row) => row.label), axisLabel: { color: chartTheme().palette.muted } },
    yAxis: { type: 'value', max: 1, axisLabel: { color: chartTheme().palette.muted, formatter: (value) => `${Math.round(value * 100)}%` } },
    series: [{ name: '平均利用率', type: 'bar', data: rows.map((row) => row.avg_utilization ?? 0), itemStyle: { color: resolveVar('--state-idle'), borderRadius: [4, 4, 0, 0] }, label: { show: true, position: 'top', formatter: ({ value }) => `${Math.round(Number(value) * 100)}%` } }],
  });
}

/** 总览/能源：四时段能耗贡献。 */
export function renderEnergyBands(el, energy = {}) {
  const rows = energy.time_bands ?? [];
  return renderAnalysisChart(el, {
    tooltip: { ...chartTheme().tooltip, trigger: 'axis', valueFormatter: (value) => `${Number(value).toFixed(1)} kWh` },
    grid: { left: 48, right: 18, top: 18, bottom: 34 },
    xAxis: { type: 'category', data: rows.map((row) => row.label), axisLabel: { color: chartTheme().palette.muted, interval: 0, rotate: 18 } },
    yAxis: { type: 'value', name: 'kWh', axisLabel: { color: chartTheme().palette.muted } },
    series: [{ type: 'bar', data: rows.map((row) => row.energy_kwh ?? 0), itemStyle: { color: resolveVar('--state-charging'), borderRadius: [4, 4, 0, 0] } }],
  });
}

/** 预测页：读取真实模型 metadata；服务没有暴露验证指标时明确降级。 */
export function renderForecastQuality(el, metadata = {}) {
  const metrics = metadata.metrics ?? {};
  const rows = [['MAE', metrics.mae], ['RMSE', metrics.rmse]].filter(([, value]) => Number.isFinite(Number(value)));
  return renderAnalysisChart(el, {
    tooltip: { ...chartTheme().tooltip, trigger: 'axis', valueFormatter: (value) => `${Number(value).toFixed(2)} kWh` },
    grid: { left: 54, right: 18, top: 26, bottom: 42 },
    xAxis: { type: 'category', data: rows.length ? rows.map(([label]) => label) : ['验证指标'], axisLabel: { color: chartTheme().palette.muted } },
    yAxis: { type: 'value', name: '误差 kWh', axisLabel: { color: chartTheme().palette.muted } },
    series: rows.length ? [{ type: 'bar', data: rows.map(([, value]) => Number(value)), itemStyle: { color: resolveVar('--night-focus'), borderRadius: [4, 4, 0, 0] }, label: { show: true, position: 'top', formatter: ({ value }) => Number(value).toFixed(2) } }] : emptySeries('服务未提供'),
    graphic: [{ type: 'text', left: 'center', bottom: 5, style: { text: metadata.validation_rows ? `验证集 ${metadata.validation_rows.toLocaleString()} 行` : '模型服务未提供验证指标', fill: chartTheme().palette.muted, fontSize: 11 } }],
  });
}

/** 预测页：以不同站点的 1 小时预测值替代缺失的真实残差，标题明确口径。 */
export function renderForecastResiduals(el, items = []) {
  const rows = items.filter((row) => Number(row.horizon_hours) === 1).slice(0, 10);
  return renderAnalysisChart(el, {
    tooltip: { ...chartTheme().tooltip, trigger: 'axis', valueFormatter: (value) => `${Number(value).toFixed(1)} kWh` },
    grid: { left: 58, right: 18, top: 18, bottom: 32 },
    xAxis: { type: 'category', data: rows.map((row) => `S${row.station_id}`), axisLabel: { color: chartTheme().palette.muted } },
    yAxis: { type: 'value', name: '预测值', axisLabel: { color: chartTheme().palette.muted } },
    series: rows.length ? [{ name: '1 小时预测', type: 'bar', data: rows.map((row) => Number(row.predicted_value ?? 0)), itemStyle: { color: resolveVar('--state-idle'), borderRadius: [4, 4, 0, 0] } }] : emptySeries('暂无预测'),
    graphic: [{ type: 'text', left: 'center', bottom: 5, style: { text: rows.length ? '当前批次未提供真实残差/置信区间' : '分析服务不可用', fill: chartTheme().palette.muted, fontSize: 11 } }],
  });
}

/** 预测页：推荐得分和预警站点并列，避免只显示一条文字结论。 */
export function renderForecastActions(el, recommendations = [], alerts = []) {
  const recommendationRows = recommendations.slice(0, 5);
  const labels = recommendationRows.map((row) => `推荐 S${row.station_id}`);
  return renderAnalysisChart(el, {
    tooltip: { ...chartTheme().tooltip, trigger: 'axis', valueFormatter: (value) => Number(value).toFixed(3) },
    grid: { left: 86, right: 24, top: 18, bottom: 32 },
    xAxis: { type: 'value', min: -1, max: 1, axisLabel: { color: chartTheme().palette.muted } },
    yAxis: { type: 'category', data: labels.length ? labels : ['暂无推荐'], axisLabel: { color: chartTheme().palette.muted } },
    series: [{ name: '推荐得分', type: 'bar', data: recommendationRows.length ? recommendationRows.map((row) => Number(row.score ?? 0)) : [0], itemStyle: { color: resolveVar('--state-idle'), borderRadius: [0, 4, 4, 0] } }],
    graphic: [{ type: 'text', left: 'center', bottom: 5, style: { text: `开放预警 ${alerts.length} 条 · 历史 P95 基线`, fill: alerts.length ? resolveVar('--state-fault') : chartTheme().palette.muted, fontSize: 11 } }],
  });
}

/** 用户页：RFM 样本散点，x=近度，y=金额，点大小=频次。 */
export function renderRfmMatrix(el, mining = {}) {
  const rows = (mining.top_users ?? []).slice(0, 24);
  return renderAnalysisChart(el, {
    tooltip: { ...chartTheme().tooltip, trigger: 'item', formatter: (params) => `用户 ${params.data[3]}<br/>近度 ${params.data[0]} 天 · 金额 ¥${(Number(params.data[1]) / 100).toFixed(0)} · ${params.data[2]} 次` },
    grid: { left: 52, right: 18, top: 18, bottom: 40 },
    xAxis: { type: 'value', name: '距最近消费（天）', inverse: true, axisLabel: { color: chartTheme().palette.muted } },
    yAxis: { type: 'value', name: '累计消费（元）', axisLabel: { color: chartTheme().palette.muted, formatter: (value) => `¥${Math.round(value / 100)}` } },
    series: rows.length ? [{ type: 'scatter', data: rows.map((row) => [row.recency_days ?? 999, row.monetary ?? 0, Math.max(8, (row.frequency ?? 0) * 4), row.user_id]), symbolSize: (value) => Math.min(32, Number(value[2])), itemStyle: { color: resolveVar('--night-focus'), opacity: 0.82 } }] : emptySeries('暂无 RFM 样本'),
  });
}

/** 用户与设备页：异常桩 z-score + 功率分布的设备风险视图。 */
export function renderEquipmentRisk(el, equipment = {}, mining = {}) {
  const rows = (mining.top_anomalies ?? []).slice(0, 8).reverse();
  return renderAnalysisChart(el, {
    tooltip: { ...chartTheme().tooltip, trigger: 'axis', valueFormatter: (value) => `z=${Number(value).toFixed(2)}` },
    grid: { left: 78, right: 18, top: 18, bottom: 34 },
    xAxis: { type: 'value', name: 'z-score', axisLabel: { color: chartTheme().palette.muted } },
    yAxis: { type: 'category', data: rows.map((row) => row.pile_code ?? `桩 ${row.pile_id}`), axisLabel: { color: chartTheme().palette.muted } },
    series: rows.length ? [{ type: 'bar', data: rows.map((row) => Number(row.z_score ?? 0)), itemStyle: { color: resolveVar('--state-fault'), borderRadius: [0, 4, 4, 0] } }] : emptySeries('暂无异常桩'),
    graphic: [{ type: 'text', left: 'center', bottom: 5, style: { text: `设备总量 ${Object.values(equipment.status_counts ?? {}).reduce((sum, value) => sum + Number(value), 0)} 台`, fill: chartTheme().palette.muted, fontSize: 11 } }],
  });
}

/** 订单页：状态计数漏斗。 */
export function renderOrderFunnel(el, orders = {}) {
  const rows = orders.status_counts ?? [];
  return renderAnalysisChart(el, {
    tooltip: { ...chartTheme().tooltip, trigger: 'item' },
    series: [{ type: 'funnel', left: '8%', top: 16, bottom: 18, width: '84%', min: 0, max: Math.max(1, ...rows.map((row) => Number(row.count ?? 0))), minSize: '20%', maxSize: '92%', sort: 'descending', gap: 3, label: { color: chartTheme().palette.text, formatter: ({ name, value }) => `${name}  ${value}` }, itemStyle: { borderColor: resolveVar('--night-bg'), borderWidth: 1 }, data: rows.map((row) => ({ name: row.label, value: row.count })) }],
  });
}

/** 能源页：四时段能耗 + 订单量相关系数。 */
export function renderEnergyPeaks(el, energy = {}) {
  const rows = energy.time_bands ?? [];
  return renderAnalysisChart(el, {
    tooltip: { ...chartTheme().tooltip, trigger: 'axis', valueFormatter: (value) => `${Number(value).toFixed(1)} kWh` },
    grid: { left: 54, right: 18, top: 18, bottom: 40 },
    xAxis: { type: 'category', data: rows.map((row) => row.label), axisLabel: { color: chartTheme().palette.muted, interval: 0, rotate: 18 } },
    yAxis: { type: 'value', name: 'kWh', axisLabel: { color: chartTheme().palette.muted } },
    series: [{ type: 'bar', data: rows.map((row) => row.energy_kwh ?? 0), itemStyle: { color: resolveVar('--state-charging'), borderRadius: [4, 4, 0, 0] }, label: { show: true, position: 'top', formatter: ({ value }) => Number(value).toFixed(0) } }],
    graphic: [{ type: 'text', left: 'center', bottom: 4, style: { text: `订单量—能耗 Pearson r = ${Number(energy.order_energy_correlation ?? 0).toFixed(2)} · 峰值占比 ${(Number(energy.peak_share ?? 0) * 100).toFixed(1)}%`, fill: chartTheme().palette.muted, fontSize: 11 } }],
  });
}

/** 收益页：日营收与 OLS 拟合线。 */
export function renderRevenueRegression(el, revenue = {}) {
  const rows = revenue.daily ?? [];
  const values = rows.map((row) => Number(row.revenue_cents ?? 0));
  const mean = values.reduce((sum, value) => sum + value, 0) / Math.max(1, values.length);
  const xMean = (values.length - 1) / 2;
  const denominator = values.reduce((sum, _value, index) => sum + (index - xMean) ** 2, 0);
  const slope = denominator ? values.reduce((sum, value, index) => sum + (index - xMean) * (value - mean), 0) / denominator : 0;
  const fit = values.map((_value, index) => mean + slope * (index - xMean));
  return renderAnalysisChart(el, {
    tooltip: { ...chartTheme().tooltip, trigger: 'axis', valueFormatter: (value) => formatCents(Math.round(value)) },
    legend: { ...chartTheme().legend, top: 0 },
    grid: { left: 60, right: 16, top: 34, bottom: 30 },
    xAxis: { type: 'category', data: rows.map((row) => row.date?.slice(5) ?? '—'), axisLabel: { color: chartTheme().palette.muted } },
    yAxis: { type: 'value', axisLabel: { color: chartTheme().palette.muted, formatter: (value) => `¥${Math.round(value / 100)}` } },
    series: [{ name: '实际营收', type: 'bar', data: values, itemStyle: { color: resolveVar('--state-idle'), opacity: 0.8 } }, { name: 'OLS 拟合', type: 'line', data: fit, smooth: false, lineStyle: { color: resolveVar('--night-focus'), width: 2, type: 'dashed' } }],
    graphic: [{ type: 'text', left: 'center', bottom: 3, style: { text: `斜率 ${(Number(revenue.trend?.slope_cents_per_day ?? slope) / 100).toFixed(2)} 元/日 · R² ${Number(revenue.trend?.r2 ?? 0).toFixed(2)}`, fill: chartTheme().palette.muted, fontSize: 11 } }],
  });
}

/** 站点页：聚类平均利用率与累计营收 Pareto。 */
export function renderStationClusters(el, mining = {}) {
  const centroids = mining.centroids ?? [];
  const pareto = mining.pareto ?? [];
  return renderAnalysisChart(el, {
    tooltip: { ...chartTheme().tooltip, trigger: 'axis' },
    legend: { ...chartTheme().legend, top: 0 },
    grid: { left: 50, right: 48, top: 34, bottom: 34 },
    xAxis: { type: 'category', data: centroids.map((row) => row.label), axisLabel: { color: chartTheme().palette.muted } },
    yAxis: [{ type: 'value', max: 1, axisLabel: { color: chartTheme().palette.muted, formatter: (value) => `${Math.round(value * 100)}%` } }, { type: 'value', max: 1, axisLabel: { color: chartTheme().palette.muted, formatter: (value) => `${Math.round(value * 100)}%` } }],
    series: [{ name: '聚类平均利用率', type: 'bar', data: centroids.map((row) => row.avg_utilization ?? 0), itemStyle: { color: resolveVar('--state-charging'), borderRadius: [4, 4, 0, 0] } }, { name: '累计营收占比', type: 'line', yAxisIndex: 1, data: centroids.map((_row, index) => pareto.length ? pareto[Math.min(pareto.length - 1, Math.round((index + 1) * pareto.length / Math.max(1, centroids.length)) - 1)]?.cumulative_share ?? 0 : 0), lineStyle: { color: resolveVar('--state-idle'), type: 'dashed' } }],
  });
}

/** 服务页：完成订单时长分布。 */
export function renderDurationBuckets(el, service = {}) {
  const rows = service.duration_buckets ?? [];
  return renderAnalysisChart(el, {
    tooltip: { ...chartTheme().tooltip, trigger: 'axis' },
    grid: { left: 46, right: 18, top: 18, bottom: 34 },
    xAxis: { type: 'category', data: rows.map((row) => row.label), axisLabel: { color: chartTheme().palette.muted } },
    yAxis: { type: 'value', name: '完成订单', axisLabel: { color: chartTheme().palette.muted } },
    series: [{ type: 'bar', data: rows.map((row) => row.count ?? 0), itemStyle: { color: resolveVar('--night-focus'), borderRadius: [4, 4, 0, 0] }, label: { show: true, position: 'top' } }],
  });
}

/** 服务页：按日完成率与基线对照。 */
export function renderServiceControl(el, orders = {}, service = {}) {
  const daily = orders.daily ?? [];
  const baseline = Number(service.service_control?.baseline_completion_rate ?? service.completion_rate ?? 0) * 100;
  return renderAnalysisChart(el, {
    tooltip: { ...chartTheme().tooltip, trigger: 'axis', valueFormatter: (value) => `${Number(value).toFixed(1)}%` },
    grid: { left: 48, right: 18, top: 22, bottom: 30 },
    xAxis: { type: 'category', data: daily.map((row) => row.date?.slice(5) ?? '—'), axisLabel: { color: chartTheme().palette.muted } },
    yAxis: { type: 'value', min: 0, max: 100, axisLabel: { color: chartTheme().palette.muted, formatter: '{value}%' } },
    series: [{ name: '日完成率', type: 'line', smooth: true, data: daily.map((row) => row.total ? (Number(row.completed ?? 0) / Number(row.total)) * 100 : 0), lineStyle: { color: resolveVar('--night-focus') }, areaStyle: { color: 'rgba(77, 215, 255, 0.12)' } }, { name: '总体基线', type: 'line', data: daily.map(() => baseline), symbol: 'none', lineStyle: { color: resolveVar('--state-reserved'), type: 'dashed' } }],
  });
}

/** 服务页：评价事实缺失时展示数据质量状态，而不是生成虚构星级。 */
export function renderServiceDataAvailability(el, service = {}) {
  const available = service.rating_available === true;
  return renderAnalysisChart(el, {
    series: [{ type: 'pie', radius: ['55%', '76%'], center: ['50%', '46%'], label: { color: chartTheme().palette.text, formatter: available ? '评价事实\n可用' : '评价事实\n缺失' }, data: [{ name: available ? '评价事实可用' : '评价事实缺失', value: 1, itemStyle: { color: available ? resolveVar('--state-idle') : resolveVar('--state-reserved') } }] }],
    graphic: [{ type: 'text', left: 'center', bottom: 6, style: { text: available ? '可扩展星级与情感分析' : (service.rating_note ?? '当前使用订单代理指标'), fill: chartTheme().palette.muted, fontSize: 11 } }],
  });
}

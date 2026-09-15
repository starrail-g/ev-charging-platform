// 主装配：runtime config → demo 数据 → 指标/关注队列/利用率 → 地图（自动降级）→ 图表。
// 任何局部组件失败只显示局部 error 态，不让整个主屏白屏。
// 第二阶段：?source=analytics 切换分析链路（/api/dashboard，Vue 筛选控件，默认拓扑地图）。
import { DemoDataProvider, LiveDataProvider } from './data-provider.js';
import { ApiDataProvider } from './api-data-provider.js';
import { AnalysisProvider } from './analysis-provider.js';
import { createStationCatalog } from './station-catalog.js';
import { mapPileStatus } from './status-map.js';
import { statusIconSvg } from './status-icons.js';
import { createDashboardState, deriveOverviewCards } from './dashboard-state.js';
import { MapSurface } from './map-surface.js';
import { TopologyMapRenderer } from './topology-map-renderer.js';
import { TencentMapRenderer } from './tencent-map-renderer.js';
import {
  renderLoadChart, renderStateDonut, renderRevenueTrend,
  renderUserSegments, renderEquipmentOverview, renderOrderTrend,
  renderEnergyProfile, renderStationRanking, renderServiceQuality, renderForecastHorizon,
  renderHealthScore, renderAnomalyScan, renderClusterSummary, renderEnergyBands,
  renderForecastQuality, renderForecastResiduals, renderForecastActions,
  renderRfmMatrix, renderEquipmentRisk, renderOrderFunnel, renderEnergyPeaks,
  renderRevenueRegression, renderStationClusters, renderDurationBuckets,
  renderServiceControl, renderServiceDataAvailability,
} from './charts.js';
import { renderPageState, statePresentation } from './state-view.js';
import { analysisSourceAttribution } from './analysis-source.js';

const state = createDashboardState({ liveMode: false });
const params = new URLSearchParams(location.search);
// 大屏模式：default = ML 链路（默认数据源即分析服务），analytics = 已发布批次链路。
// analytics 模式下 ML 分析区属于另一条数据链，必须显式标注来源（PR #24 评审 P2）。
let dashboardMode = 'default';

function announce(message) {
  const region = document.getElementById('globalState');
  if (region) region.textContent = message;
}

/** 把筛选条件写回 URL（刷新/分享保留窗口）；只改 query string，不触发导航。 */
function syncUrlQuery(query) {
  const url = new URL(location.href);
  const setOr = (key, value) => {
    if (value === null || value === undefined || value === '') url.searchParams.delete(key);
    else url.searchParams.set(key, String(value));
  };
  setOr('start', query.start);
  setOr('end', query.end);
  setOr('stationId', query.stationId);
  history.replaceState(null, '', url);
}

function fmtUtilization(ratio) {
  return `${Math.round((ratio ?? 0) * 100)}%`;
}

function renderAnalysis(result, options = {}) {
  const attribution = analysisSourceAttribution({ ok: result.ok === true, independent: options.independent === true });
  const status = document.getElementById('analysisStatus');
  const summary = document.getElementById('analysisSummary');
  if (!status || !summary) return;
  summary.innerHTML = '';
  const forecastMethod = document.querySelector('[data-forecast-method]');
  if (forecastMethod) {
    const algorithm = String(result.health?.metadata?.algorithm ?? '');
    forecastMethod.textContent = algorithm.includes('RandomForest') ? '随机森林回归' : result.ok ? '站点时段基线' : '模型回归';
  }
  renderForecastWorkbench(result, { metaPrefix: attribution.metaPrefix });
  renderForecastSupportingModules(result);
  if (!result.ok) {
    status.textContent = result.code === 'ANALYSIS_NOT_CONFIGURED' ? '未配置' : '不可用';
    status.dataset.state = attribution.state;
    const note = document.createElement('p');
    note.className = 'model-note';
    note.textContent = `${result.message}；当前页面保留基础运营指标。`;
    summary.append(note);
    return;
  }
  status.textContent = attribution.chip;
  status.dataset.state = attribution.state;
  const forecastItems = result.forecast?.items ?? [];
  const oneHour = forecastItems.filter((item) => item.horizon_hours === 1);
  const topForecast = [...oneHour].sort((a, b) => b.predicted_value - a.predicted_value)[0];
  const recommendation = result.recommendations?.items?.[0];
  const alertCount = result.alerts?.items?.length ?? 0;
  const rows = [
    ['1 小时峰值负荷', topForecast ? `${topForecast.predicted_value.toFixed(1)} kWh · 站点 ${topForecast.station_id}` : '暂无'],
    ['低拥堵推荐', recommendation ? `站点 ${recommendation.station_id} · 空闲率 ${(recommendation.predicted_idle_rate * 100).toFixed(0)}%` : '暂无候选'],
    ['开放预警', `${alertCount} 条 · 历史 P95 基线`],
  ];
  const list = document.createElement('dl');
  list.className = 'analysis-list';
  for (const [label, value] of rows) {
    const term = document.createElement('dt');
    term.textContent = label;
    const desc = document.createElement('dd');
    desc.textContent = value;
    list.append(term, desc);
  }
  summary.append(list);
  const meta = document.createElement('p');
  meta.className = 'analysis-meta';
  meta.textContent = `模型 ${result.forecast?.model_version ?? '—'} · 截止 ${result.forecast?.data_cutoff_at ?? '—'}`;
  summary.append(meta);
  if (attribution.note) {
    // analytics 模式：显式标注 ML 区为独立数据来源（不受本页筛选影响，评审 P2）。
    const sourceNote = document.createElement('p');
    sourceNote.className = 'model-note analysis-source-note';
    sourceNote.textContent = attribution.note;
    summary.append(sourceNote);
  }
}

function renderForecastSupportingModules(result) {
  const workbench = document.getElementById('analysis-workbench');
  const wasHidden = workbench?.hidden;
  if (workbench) workbench.hidden = false;
  document.querySelectorAll('[data-analysis-group="forecast"]').forEach((panel) => { panel.hidden = false; });
  const recommendations = result.ok ? (result.recommendations?.items ?? []) : [];
  const alerts = result.ok ? (result.alerts?.items ?? []) : [];
  const definitions = [
    ['analysis-forecast-quality', () => renderForecastQuality(document.getElementById('analysis-forecast-quality'), result.health?.metadata ?? {})],
    ['analysis-forecast-residuals', () => renderForecastResiduals(document.getElementById('analysis-forecast-residuals'), result.ok ? (result.forecast?.items ?? []) : [])],
    ['analysis-forecast-actions', () => renderForecastActions(document.getElementById('analysis-forecast-actions'), recommendations, alerts)],
  ];
  for (const [id, factory] of definitions) {
    try {
      const chart = factory();
      chartsById[id] = chart;
      if (!chartInstances.includes(chart)) chartInstances.push(chart);
    } catch (error) {
      renderLocalError(document.getElementById(id), error?.message ?? '预测分析初始化失败');
    }
  }
  showWorkspacePage(currentWorkspacePage);
  if (workbench && wasHidden && currentWorkspacePage === 'overview') workbench.hidden = true;
}

function renderForecastWorkbench(result, options = {}) {
  const target = document.getElementById('analysis-forecast-meta');
  const chart = document.getElementById('analysis-forecast');
  if (!target || !chart) return;
  const metaPrefix = options.metaPrefix ?? '';
  const items = result.ok ? (result.forecast?.items ?? []) : [];
  chart.parentElement?.querySelector('.forecast-insights')?.remove();
  if (!items.length) {
    target.textContent = result.message ? `${result.message}；预测图表暂不可用` : '等待分析服务返回预测结果';
    return;
  }
  try {
    chartsById['analysis-forecast'] = renderForecastHorizon(chart, items);
    if (!chartInstances.includes(chartsById['analysis-forecast'])) chartInstances.push(chartsById['analysis-forecast']);
    target.textContent = `${metaPrefix}模型 ${result.forecast?.model_version ?? '—'} · 数据截止 ${result.forecast?.data_cutoff_at ?? '—'} · 共 ${items.length} 条预测`;
    const insight = document.createElement('div');
    insight.className = 'forecast-insights';
    const recommendation = result.recommendations?.items?.[0];
    const alertCount = result.alerts?.items?.length ?? 0;
    insight.innerHTML = `<span><b>调度建议</b>${recommendation ? `优先引导至站点 ${recommendation.station_id}（预测空闲率 ${(Number(recommendation.predicted_idle_rate ?? 0) * 100).toFixed(0)}%）` : '暂无低拥堵候选'}</span><span><b>风险扫描</b>${alertCount ? `${alertCount} 个站点超过历史 P95 基线` : '未发现超过历史 P95 的站点'}</span>`;
    chart.parentElement?.append(insight);
  } catch (error) {
    target.textContent = `预测图表初始化失败：${error?.message ?? '未知错误'}`;
  }
}

// 分帧让出主线程：慢机/软渲染上单任务连渲多张图表会撞上浏览器"脚本运行超时"终止
// （该终止不可被 try/catch 捕获）。逐批 yield 让每个 chunk 独立成任务——单批被终止
// 最多损失该批，其后的渲染照常完成，避免"一图超时、整页停在半渲染"。
const yieldToMain = () => new Promise((resolve) => setTimeout(resolve, 0));

async function renderOverviewMining(model, chartRegistry, instances) {
  const analytics = model.analytics ?? {};
  const definitions = [
    ['overview-health', () => renderHealthScore(document.getElementById('overview-health'), model.metrics, analytics)],
    ['overview-anomalies', () => renderAnomalyScan(document.getElementById('overview-anomalies'), analytics.equipment_mining)],
    ['overview-clusters', () => renderClusterSummary(document.getElementById('overview-clusters'), analytics.station_mining)],
    ['overview-energy-bands', () => renderEnergyBands(document.getElementById('overview-energy-bands'), analytics.energy)],
  ];
  let processed = 0;
  for (const [id, factory] of definitions) {
    try {
      const chart = factory();
      chartRegistry[id] = chart;
      instances.push(chart);
    } catch (error) {
      renderLocalError(document.getElementById(id), error?.message ?? '总览分析初始化失败');
    }
    processed += 1;
    if (processed % 2 === 0) await yieldToMain();
  }
}

function statusChip(protocol) {
  const meta = mapPileStatus(protocol);
  const chip = document.createElement('span');
  chip.className = `status-chip state-${meta.protocol}`;
  chip.innerHTML = `${statusIconSvg(meta.protocol)}<span>${meta.label}</span>`;
  return chip;
}

/* ---------- 左栏指标 ---------- */

function renderOverviewCards(metrics) {
  const strip = document.getElementById('overview-metrics');
  strip.innerHTML = '';
  for (const card of deriveOverviewCards(metrics)) {
    const article = document.createElement('article');
    article.className = 'metric-card';
    const label = document.createElement('span');
    label.className = 'metric-label';
    label.textContent = card.label;
    const value = document.createElement('span');
    value.className = 'metric-value';
    value.textContent = card.value;
    article.append(label, value);
    if (card.sub) {
      const sub = document.createElement('span');
      sub.className = 'metric-value metric-sub';
      sub.textContent = card.sub;
      article.append(sub);
    }
    strip.appendChild(article);
  }
}

/* ---------- 右栏：利用率排行 ---------- */

function renderUtilization(model) {
  document.getElementById('avgUtilization').textContent = fmtUtilization(
    model.metrics.avgStationUtilization
  );
  const list = document.getElementById('utilizationList');
  list.innerHTML = '';
  for (const row of model.stationUtilization ?? []) {
    const item = document.createElement('li');
    item.className = 'util-item';
    const name = document.createElement('span');
    name.className = 'util-name';
    name.textContent = row.name;
    const value = document.createElement('span');
    value.className = 'util-value';
    value.textContent = fmtUtilization(row.utilization);
    const track = document.createElement('span');
    track.className = 'util-track';
    const bar = document.createElement('span');
    bar.className = 'util-bar';
    bar.style.width = `${Math.round(row.utilization * 100)}%`;
    track.append(bar);
    item.append(name, value, track);
    list.appendChild(item);
  }
}

/* ---------- 右栏：实时关注队列 + 异常聚焦模式 ---------- */

function attentionPiles(model) {
  const stationById = new Map(model.stations.map((station) => [station.id, station]));
  const items = [];
  for (const pile of model.piles) {
    if (pile.status === 'fault' || pile.status === 'offline') {
      items.push({ pile, meta: mapPileStatus(pile.status), station: stationById.get(pile.stationId) });
    }
  }
  return items;
}

function clearAlertSelection() {
  document.querySelectorAll('.alert-item').forEach((item) => {
    item.classList.remove('alert-active');
    item.setAttribute('aria-pressed', 'false');
  });
}

function renderFocusDetail(model, item, onClose) {
  const detail = document.getElementById('focus-detail');
  const station = item.station;
  detail.innerHTML = '';
  detail.hidden = false;

  const head = document.createElement('div');
  head.className = 'fd-head';
  const icon = document.createElement('span');
  icon.className = `status-icon state-${item.meta.protocol}`;
  icon.innerHTML = statusIconSvg(item.meta.protocol);
  const title = document.createElement('span');
  title.textContent = station ? station.name : '未知站点';
  const close = document.createElement('button');
  close.type = 'button';
  close.className = 'fd-close';
  close.setAttribute('aria-label', '退出异常聚焦');
  close.textContent = '✕';
  head.append(icon, title, close);
  detail.append(head);

  const dl = document.createElement('dl');
  const rows = [
    ['桩编号', item.pile.code],
    ['状态', item.meta.label],
    ['类型/功率', `${item.pile.type === 'fast' ? '快充' : '慢充'} · ${item.pile.powerKw} kW`],
    ['更新时间', String(model.updatedAt ?? '') + ' (UTC)'],
    ['异常说明', `${item.meta.label}桩需要现场检查或远程处置，请先在桩/站页面核实状态。`],
  ];
  for (const [dt, dd] of rows) {
    const term = document.createElement('dt');
    term.textContent = dt;
    const desc = document.createElement('dd');
    desc.textContent = dd;
    dl.append(term, desc);
  }
  detail.append(dl);
  // 关闭按钮走与 Esc / 再次点击 / 点击无告警站点相同的退出出口，
  // 不得绕过 focused 清空，否则“关闭后单击同一故障”会被误判为再次点击。
  close.addEventListener('click', () => onClose());
}

function renderAlertRail(model, { onActivate }) {
  const list = document.getElementById('alertList');
  list.innerHTML = '';
  const items = attentionPiles(model);
  document.getElementById('attentionCount').textContent = `· ${items.length} 项`;

  for (const item of items) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = `alert-item state-${item.pile.status}`;
    button.dataset.pileCode = item.pile.code;
    button.dataset.stationId = String(item.station?.id ?? '');
    button.setAttribute(
      'aria-label',
      `${item.pile.code} ${item.meta.label}，位于${item.station?.name ?? '未知站点'}，回车聚焦站点详情`
    );
    button.innerHTML = statusIconSvg(item.pile.status);

    const body = document.createElement('span');
    body.className = 'alert-body';
    const head = document.createElement('span');
    head.className = 'alert-head';
    const code = document.createElement('strong');
    code.textContent = item.pile.code;
    const tag = document.createElement('span');
    tag.className = 'alert-tag';
    tag.textContent = item.meta.label;
    head.append(code, tag);
    const meta = document.createElement('span');
    meta.className = 'alert-meta';
    meta.textContent =
      `${item.station?.name ?? '未知站点'} · ${item.pile.type === 'fast' ? '快充' : '慢充'} · ${item.pile.powerKw} kW`;
    body.append(head, meta);
    button.append(body);
    button.addEventListener('click', () => onActivate(item, button));
    list.appendChild(button);
  }
  if (items.length === 0) {
    const empty = document.createElement('p');
    empty.className = 'rail-empty';
    empty.textContent = '当前无故障或离线桩';
    list.appendChild(empty);
  }
  return items;
}

/* ---------- 局部错误 ---------- */

function renderLocalError(container, message) {
  const panel = document.createElement('div');
  panel.className = 'local-error';
  panel.setAttribute('role', 'alert');
  const title = document.createElement('strong');
  title.textContent = '该区域加载失败';
  const detail = document.createElement('span');
  detail.textContent = message;
  panel.append(title, detail);
  container.replaceChildren(panel);
}

/* ---------- 经营板块页签（紧凑态） ---------- */

// 页签与面板是 index.html 静态结构，只在 boot 绑定一次事件；
// 每次数据渲染后以 showChartTab(targetId) 切换（重试不重复绑监听）。
let chartsById = {};
let chartInstances = [];
let currentChartTab = 'chart-load';
let currentAnalysisTab = 'users';
let currentWorkspacePage = 'overview';

const ANALYSIS_PAGE_BY_TAB = {
  forecast: 'forecast', users: 'audience', equipment: 'audience',
  orders: 'operations', energy: 'operations', revenue: 'business',
  stations: 'business', service: 'service',
};
const WORKSPACE_PAGE_META = {
  overview: ['网络总览', '站点地图、设备健康与经营态势的实时总览'],
  forecast: ['智能预测', 'Spark MLlib 负荷预测、低拥堵推荐与历史基线预警'],
  audience: ['用户与设备', '从用户价值分群到设备利用率，观察供需匹配效率'],
  operations: ['订单与能源', '订单转化、履约质量与小时级用能峰谷联动'],
  business: ['收益与站点', '营收趋势与站点经营排行，定位增长与容量机会'],
  service: ['评价与服务', '以履约、取消、复购等可追溯指标衡量服务质量'],
};

function showWorkspacePage(target) {
  const page = target || 'overview';
  currentWorkspacePage = page;
  // 总览的两块态势内容在分析页让出空间；分析工作台在其余页面占满内容区。
  document.querySelectorAll('[data-workspace-section="overview"]').forEach((section) => {
    section.hidden = page !== 'overview';
  });
  const workbench = document.getElementById('analysis-workbench');
  if (workbench) workbench.hidden = page === 'overview';
  document.querySelectorAll('.workspace-nav-item').forEach((item) => {
    const active = item.dataset.workspacePage === page;
    item.classList.toggle('is-active', active);
    if (active) item.setAttribute('aria-current', 'page'); else item.removeAttribute('aria-current');
  });
  document.querySelectorAll('.analysis-panel').forEach((panel) => {
    panel.hidden = page === 'overview' || panel.dataset.analysisGroup !== page;
  });
  const [title, subtitle] = WORKSPACE_PAGE_META[page] ?? WORKSPACE_PAGE_META.overview;
  const titleEl = document.querySelector('.workbench-title');
  const subtitleEl = document.querySelector('.workbench-subtitle');
  if (titleEl) titleEl.textContent = title;
  if (subtitleEl) {
    // 分析模式下智能预测页的 ML 区来自独立服务链路：页头同步说明来源（PR #24 评审 P2）。
    subtitleEl.textContent = page === 'forecast' && dashboardMode === 'analytics'
      ? `${subtitle}（本页 ML 区来自独立 ML 服务链路，不受当前窗口/站点筛选影响）`
      : subtitle;
  }
  requestAnimationFrame(() => {
    chartInstances.forEach((chart) => chart?.resize());
  });
  const nextHash = page === 'overview' ? '' : `#${page}`;
  if (history.replaceState && location.hash !== nextHash) history.replaceState(null, '', nextHash || location.pathname + location.search);
  announce(`已切换至${document.querySelector(`.workspace-nav-item[data-workspace-page="${page}"] b`)?.textContent ?? '分析页面'}`);
}

function showChartTab(targetId) {
  currentChartTab = targetId;
  const panels = [...document.querySelectorAll('.chart-panel')];
  for (const panel of panels) {
    panel.dataset.tabHidden = panel.dataset.chartPanel === targetId ? 'false' : 'true';
  }
  for (const tab of document.querySelectorAll('.ops-tab')) {
    const isActive = tab.dataset.chartTab === targetId;
    tab.classList.toggle('is-active', isActive);
    tab.setAttribute('aria-pressed', String(isActive));
  }
  requestAnimationFrame(() => {
    chartsById[targetId]?.resize();
  });
}

function setupChartTabsOnce() {
  const tabs = [...document.querySelectorAll('.ops-tab')];
  for (const tab of tabs) {
    tab.addEventListener('click', () => showChartTab(tab.dataset.chartTab));
  }
  showChartTab('chart-load'); // 默认 24h（spec §5.5）；全景态 CSS 忽略 hidden 标记，三图同显
}

function showAnalysisTab(target) {
  currentAnalysisTab = target;
  const page = ANALYSIS_PAGE_BY_TAB[target] ?? 'audience';
  if (currentWorkspacePage !== 'overview') showWorkspacePage(page);
  for (const tab of document.querySelectorAll('.analysis-tab')) {
    const active = tab.dataset.analysisTab === target;
    tab.classList.toggle('is-active', active);
    tab.setAttribute('aria-pressed', String(active));
  }
  requestAnimationFrame(() => chartsById[`analysis-${target}`]?.resize());
}

function setupAnalysisTabsOnce() {
  for (const tab of document.querySelectorAll('.analysis-tab')) {
    tab.addEventListener('click', () => showAnalysisTab(tab.dataset.analysisTab));
  }
  showAnalysisTab('users');
}

function setupWorkspaceNavigation() {
  for (const item of document.querySelectorAll('.workspace-nav-item')) {
    item.addEventListener('click', () => showWorkspacePage(item.dataset.workspacePage));
  }
  const hashPage = (location.hash || '').slice(1);
  const valid = [...document.querySelectorAll('.workspace-nav-item')].some((item) => item.dataset.workspacePage === hashPage);
  showWorkspacePage(valid ? hashPage : 'overview');
  window.addEventListener('hashchange', () => {
    const next = (location.hash || '').slice(1);
    if ([...document.querySelectorAll('.workspace-nav-item')].some((item) => item.dataset.workspacePage === next)) showWorkspacePage(next);
  });
}

async function renderAnalysisWorkbench(model, chartRegistry, instances) {
  const workbench = document.getElementById('analysis-workbench');
  const wasHidden = workbench?.hidden;
  // ECharts 需要可见容器才能计算初始尺寸；总览首屏隐藏分析页时先临时展开，
  // 完成实例化后由 showWorkspacePage 恢复正确页面状态。
  if (workbench) workbench.hidden = false;
  document.querySelectorAll('.analysis-panel').forEach((panel) => { panel.hidden = false; });
  const analytics = model.analytics ?? {};
  const meta = document.getElementById('analysisSnapshotMeta');
  if (meta) meta.textContent = `快照 ${model.updatedAt ?? '—'} · ${model.meta?.source ?? 'Schema v0.4'}`;
  const navFreshness = document.getElementById('navFreshness');
  if (navFreshness) navFreshness.textContent = model.updatedAt ? String(model.updatedAt).slice(11, 16) : '—';
  const definitions = [
    ['analysis-users', () => renderUserSegments(document.getElementById('analysis-users'), analytics.users?.segments, analytics.user_mining)],
    ['analysis-equipment', () => renderEquipmentOverview(document.getElementById('analysis-equipment'), analytics.equipment, analytics.equipment_mining)],
    ['analysis-user-cohort', () => renderRfmMatrix(document.getElementById('analysis-user-cohort'), analytics.user_mining)],
    ['analysis-equipment-risk', () => renderEquipmentRisk(document.getElementById('analysis-equipment-risk'), analytics.equipment, analytics.equipment_mining)],
    ['analysis-orders', () => renderOrderTrend(document.getElementById('analysis-orders'), analytics.orders?.daily)],
    ['analysis-energy', () => renderEnergyProfile(document.getElementById('analysis-energy'), analytics.energy?.hourly)],
    ['analysis-order-funnel', () => renderOrderFunnel(document.getElementById('analysis-order-funnel'), analytics.orders)],
    ['analysis-energy-peaks', () => renderEnergyPeaks(document.getElementById('analysis-energy-peaks'), analytics.energy)],
    ['analysis-revenue', () => renderRevenueTrend(document.getElementById('analysis-revenue'), (analytics.revenue?.daily ?? []).map((row) => row.revenue_cents), model.updatedAt)],
    ['analysis-stations', () => renderStationRanking(document.getElementById('analysis-stations'), analytics.stations)],
    ['analysis-revenue-regression', () => renderRevenueRegression(document.getElementById('analysis-revenue-regression'), analytics.revenue)],
    ['analysis-station-clusters', () => renderStationClusters(document.getElementById('analysis-station-clusters'), analytics.station_mining)],
    ['analysis-service', () => renderServiceQuality(document.getElementById('analysis-service'), analytics.service)],
    ['analysis-service-duration', () => renderDurationBuckets(document.getElementById('analysis-service-duration'), analytics.service)],
    ['analysis-service-control', () => renderServiceControl(document.getElementById('analysis-service-control'), analytics.orders, analytics.service)],
    ['analysis-service-data', () => renderServiceDataAvailability(document.getElementById('analysis-service-data'), analytics.service)],
  ];
  let processed = 0;
  for (const [id, factory] of definitions) {
    try {
      chartRegistry[id] = factory();
      instances.push(chartRegistry[id]);
    } catch (error) {
      renderLocalError(document.getElementById(id), error?.message ?? '分析图表初始化失败');
    }
    processed += 1;
    if (processed % 3 === 0) await yieldToMain(); // 16 张图分 6 批，单批被终止不拖垮整页
  }
  const users = analytics.users ?? {};
  const equipment = analytics.equipment ?? {};
  const orders = analytics.orders ?? {};
  const energy = analytics.energy ?? {};
  const revenue = analytics.revenue ?? {};
  const service = analytics.service ?? {};
  const insights = {
    users: [['用户总量', users.total], ['复购用户', users.repeat_users], ['复购率', `${Math.round((users.repeat_users / Math.max(1, users.total)) * 100)}%`]],
    equipment: [['设备总量', Object.values(equipment.status_counts ?? {}).reduce((a, b) => a + Number(b), 0)], ['模拟设备', equipment.simulated_count], ['重启次数', equipment.restart_count]],
    'user-cohort': [['RFM 样本', analytics.user_mining?.top_users?.length ?? 0], ['高价值客群', analytics.user_mining?.segments?.find((row) => row.segment === '高价值')?.count ?? 0], ['方法', 'RFM']],
    'equipment-risk': [['异常桩', analytics.equipment_mining?.anomaly_count ?? 0], ['阈值', `|z| ≥ ${analytics.equipment_mining?.threshold ?? 2}`], ['方法', 'z-score']],
    orders: [['订单总量', orders.total], ['完成率', `${Math.round((orders.completion_rate ?? 0) * 100)}%`], ['平均时长', `${Number(orders.avg_duration_minutes ?? 0).toFixed(0)} min`]],
    energy: [['累计能耗', `${Number(energy.total_kwh ?? 0).toLocaleString()} kWh`], ['单次均值', `${Number(energy.avg_session_kwh ?? 0).toFixed(1)} kWh`], ['峰值时段', energy.peak_hour == null ? '—' : `${energy.peak_hour}:00`]],
    'order-funnel': [['完成订单', orders.completed ?? 0], ['取消订单', orders.cancelled ?? 0], ['异常/活动', orders.active ?? 0]],
    'energy-peaks': [['峰值占比', `${(Number(energy.peak_share ?? 0) * 100).toFixed(1)}%`], ['能耗相关', Number(energy.order_energy_correlation ?? 0).toFixed(2)], ['时段数', energy.time_bands?.length ?? 0]],
    revenue: [['近 30 日', formatAnalysisMoney(revenue.total_30d_cents)], ['日均收益', formatAnalysisMoney(revenue.avg_daily_cents)], ['样本天数', revenue.daily?.length ?? 0]],
    stations: [['站点数', analytics.stations?.length ?? 0], ['头部站点', analytics.stations?.[0]?.name ?? '—'], ['头部营收', formatAnalysisMoney(analytics.stations?.[0]?.revenue_cents)]],
    'revenue-regression': [['日斜率', `${(Number(revenue.trend?.slope_cents_per_day ?? 0) / 100).toFixed(2)} 元`], ['拟合度 R²', Number(revenue.trend?.r2 ?? 0).toFixed(2)], ['方法', 'OLS']],
    'station-clusters': [['聚类数', analytics.station_mining?.clusters?.length ?? 0], ['高负荷站点', analytics.station_mining?.clusters?.find((row) => row.label === '高负荷')?.count ?? 0], ['累计口径', '营收 Pareto']],
    service: [['完成率', `${Math.round((service.completion_rate ?? 0) * 100)}%`], ['非取消率', `${Math.round((1 - (service.cancel_rate ?? 0)) * 100)}%`], ['平均服务时长', `${Number(service.avg_duration_minutes ?? 0).toFixed(0)} min`]],
    'service-duration': [['完成样本', (service.duration_buckets ?? []).reduce((sum, row) => sum + Number(row.count ?? 0), 0)], ['均值', `${Number(service.avg_duration_minutes ?? 0).toFixed(0)} min`], ['方法', '分桶统计']],
    'service-control': [['基线完成率', `${Math.round(Number(service.service_control?.baseline_completion_rate ?? service.completion_rate ?? 0) * 100)}%`], ['日样本', orders.daily?.length ?? 0], ['方法', '控制图']],
    'service-data': [['评价事实', service.rating_available ? '可用' : '缺失'], ['代理指标', '完成/取消/复购'], ['星级分析', service.rating_available ? '可扩展' : '暂不生成']],
  };
  const methods = {
    users: `数据挖掘：${analytics.user_mining?.method ?? 'RFM'} 客户价值分层`,
    equipment: `异常检测：${analytics.equipment_mining?.method ?? 'z-score'} · 检出 ${analytics.equipment_mining?.anomaly_count ?? 0} 个异常桩`,
    'user-cohort': '聚类画像：RFM 最近消费、频次、金额三维用户价值矩阵',
    'equipment-risk': '异常检测：累计充电次数 z-score 与功率档位联合筛查',
    stations: `聚类分析：${analytics.station_mining?.method ?? 'rule-based clustering'} · 按利用率/能耗分组`,
    orders: '漏斗分析：全部 → 完成/取消，按日追踪转化波动',
    energy: '时序分析：小时级峰谷识别与订单量双轴关联',
    'order-funnel': '漏斗分析：状态数量逐层收缩，异常与取消不混入完成口径',
    'energy-peaks': '相关分析：Pearson r 衡量订单量与能耗的同向程度',
    revenue: '趋势分析：30 日滚动序列与日均收益基线',
    service: '服务分析：履约率、取消率、复购率联合画像',
    'revenue-regression': '回归分析：普通最小二乘拟合日营收趋势，展示斜率与 R²',
    'station-clusters': '聚类 + Pareto：站点负荷层级与营收累计贡献联看',
    'service-duration': '分布分析：完成订单履约时长分桶，观察长尾效率',
    'service-control': '统计过程控制：日完成率对总体基线做波动监测',
    'service-data': '数据质量：评价表缺失则只呈现可追溯订单代理指标',
  };
  for (const [panelId, rows] of Object.entries(insights)) {
    const panel = document.querySelector(`[data-analysis-panel="${panelId}"]`);
    if (!panel) continue;
    panel.querySelector('.analysis-kpis')?.remove();
    const strip = document.createElement('div');
    strip.className = 'analysis-kpis';
    for (const [label, value] of rows) {
      const item = document.createElement('div');
      item.className = 'analysis-kpi';
      item.innerHTML = `<span>${label}</span><strong>${value ?? '—'}</strong>`;
      strip.append(item);
    }
    panel.append(strip);
    panel.querySelector('.analysis-method')?.remove();
    if (methods[panelId]) {
      const method = document.createElement('p');
      method.className = 'analysis-method';
      method.textContent = methods[panelId];
      panel.append(method);
    }
  }
  showWorkspacePage(currentWorkspacePage);
  if (workbench && wasHidden && currentWorkspacePage === 'overview') workbench.hidden = true;
}

function formatAnalysisMoney(cents) {
  if (!Number.isFinite(Number(cents))) return '—';
  return `¥${(Number(cents) / 100).toLocaleString('zh-CN', { minimumFractionDigits: 0, maximumFractionDigits: 0 })}`;
}

/* ---------- 启动 ---------- */

async function boot() {
  const config = window.__EV_CONFIG__ ?? { tencentMapJsKey: '', analysisApiBaseUrl: '', dashboardApiBaseUrl: '', demo: false };
  const analyticsMode = params.get('source') === 'analytics' || config.source === 'analytics';
  dashboardMode = analyticsMode ? 'analytics' : 'default';
  const provider = analyticsMode
    ? new ApiDataProvider({ baseUrl: '.' })
    : new LiveDataProvider(config.dashboardApiBaseUrl || config.analysisApiBaseUrl);
  const analysisProvider = new AnalysisProvider(config.analysisApiBaseUrl);
  let currentQuery = {
    start: params.get('start'),
    end: params.get('end'),
    stationId: params.get('stationId'),
  };
  let controls = null;
  if (analyticsMode) {
    document.title = '充电网络态势分析（离线批次）';
    const titleEl = document.querySelector('.command-title');
    if (titleEl) titleEl.textContent = '充电网络态势分析';
    document.body.classList.add('analytics-mode');
  }

  const shell = document.querySelector('.dashboard-shell');
  const mapContainer = document.getElementById('map-surface');
  const modeBadge = document.getElementById('mapModeBadge');
  const sourceBadge = document.getElementById('sourceBadge');
  const freshnessEl = document.getElementById('freshness');
  const pageStateEl = document.getElementById('pageState');

  // 本地演示注入入口：?state=empty|error|offline|stale
  // 只能改变呈现分支；界面永远保留“演示状态注入”标识，不伪装成真实接口结果。
  const DEMO_STATES = ['empty', 'error', 'offline', 'stale'];
  const demoState = DEMO_STATES.includes(params.get('state')) ? params.get('state') : null;

  const mapSurface = new MapSurface({
    onlineRenderer: new TencentMapRenderer(),
    topologyRenderer: new TopologyMapRenderer(),
    timeoutMs: 5000,
  });

  let lastValidModel = null; // 最后有效快照：error 保留 / offline / stale 展示底座
  let lastValidAt = null;
  let currentDeactivateFocus = null; // Esc 只注册一次，读取每轮渲染的最新退出出口
  // 营收趋势档位（A-02）：7 或 30；静态按钮只绑一次。分析批次为多周窗口 → 默认 30 日档
  //（不再只展示末 7 个点）；7 日档仍可一键切换。
  let revenueRangeDays = analyticsMode ? 30 : 7;
  // 站点下拉全集目录：过滤后的响应只含单站，不得覆盖选项表；全集缺失（分享链接
  // ?stationId=… 或筛选后刷新时首帧即为过滤态）时由目录模块独立加载完整站点表。
  // 目录使用独立 provider 实例，与主视图请求互不取消。
  const catalogProvider = new ApiDataProvider({ baseUrl: '.' });
  const stationCatalog = createStationCatalog({
    load: (query) => catalogProvider.load(query),
    onCatalog: (stations) => {
      if (controls) controls.setStations(stations);
    },
  });
  const stationFiltered = () => currentQuery.stationId !== null
    && currentQuery.stationId !== undefined && currentQuery.stationId !== '';
  function ensureStationCatalog() {
    // 目录与主视图解耦：成功/空/错误任一结果下，过滤态首帧都必须补齐下拉全集
    //（分享链接 ?stationId=…、筛选后刷新、以及失效站号导致的空窗口）。
    // controls 只在分析模式挂载：demo 模式不产生任何分析目录请求。
    if (!controls) return;
    if (stationFiltered() && !stationCatalog.stations) stationCatalog.ensure();
  }
  // 分析批次营收标签为整窗日期：按当前档位取同尾切片；长度不匹配时不再回落到批次生成日期合成横轴。
  const sliceTail = (labels, length) =>
    (Array.isArray(labels) && labels.length >= length ? labels.slice(labels.length - length) : null);

  /* ---- 页面级状态区 ---- */

  function showPageState(pageStateObject, { blocking = false, freshness = null } = {}) {
    renderPageState(pageStateEl, pageStateObject, {
      demo: demoState !== null,
      blocking,
      freshness,
    });
    document.body.classList.toggle('has-page-banner', !blocking);
    document.body.classList.toggle('page-blocking', blocking);
  }

  function hidePageState() {
    pageStateEl.hidden = true;
    document.body.classList.remove('has-page-banner', 'page-blocking');
  }

  function snapshotLabel() {
    return lastValidAt ? `最后有效快照：${lastValidAt}（UTC）` : null;
  }

  // 初次加载骨架：占位尺寸贴近真实主区，内容出现时不跳动
  function renderSkeleton() {
    const metricStrip = document.getElementById('overview-metrics');
    metricStrip.innerHTML = '';
    for (let index = 0; index < 4; index += 1) {
      const card = document.createElement('article');
      card.className = 'metric-card skel';
      card.setAttribute('aria-hidden', 'true');
      metricStrip.appendChild(card);
    }
    const mapSurfaceEl = document.getElementById('map-surface');
    mapSurfaceEl.classList.add('skel');
    const alertList = document.getElementById('alertList');
    alertList.innerHTML = '';
    for (let index = 0; index < 3; index += 1) {
      const line = document.createElement('span');
      line.className = 'skel skel-line';
      alertList.appendChild(line);
    }
  }

  function hideSkeleton() {
    const mapSurfaceEl = document.getElementById('map-surface');
    if (mapSurfaceEl) mapSurfaceEl.classList.remove('skel');
  }

  /* ---- 主体渲染（数据有效时；可被 loadDashboard 重试再次调用） ---- */

  async function renderContent(model) {
    hideSkeleton();
    renderOverviewCards(model.metrics);
    renderUtilization(model);

    // 异常聚焦状态机：每轮数据渲染重建闭包；退出出口统一经 deactivateFocus
    let focused = null;
    const deactivateFocus = async () => {
      focused = null;
      mapContainer.classList.remove('has-focus');
      document.getElementById('focus-detail').hidden = true;
      clearAlertSelection();
      await mapSurface.clearFocus();
    };
    currentDeactivateFocus = deactivateFocus;

    const activate = async (item, button) => {
      const stationId = item?.station?.id ?? null;
      if (focused && item && focused.pile.code === item.pile.code) {
        // 再次点击当前项 → 退出聚焦
        await deactivateFocus();
        return;
      }
      await deactivateFocus(); // 清残留（含渲染器高亮与 focused）
      focused = item;
      if (button) {
        button.classList.add('alert-active');
        button.setAttribute('aria-pressed', 'true');
      }
      mapContainer.classList.add('has-focus'); // 非相关节点降权
      renderFocusDetail(model, item, deactivateFocus);
      if (stationId) {
        mapSurface.focusStation(stationId).catch(() => {});
      }
    };

    const alertItems = renderAlertRail(model, { onActivate: activate });

    // 地图激活统一入口：拓扑节点点击/回车与腾讯 marker 点击都收敛到这里，
    // 收到的是站点 id（不做业务类型转换）；命中告警项则聚焦，否则退出聚焦。
    const activateStationNode = (stationId) => {
      const target = alertItems.find((item) => item.station?.id === Number(stationId));
      if (target) {
        const button = document.querySelector(`.alert-item[data-station-id="${stationId}"]`);
        activate(target, button).catch(() => {});
      } else {
        deactivateFocus().catch(() => {}); // 点击无告警站点 → 退出聚焦
      }
    };

    // 地图：离线（含 offline 演示注入）强制拓扑，重试时 surface 内部先清理旧渲染器
    const onlineAvailable = typeof navigator !== 'undefined' ? navigator.onLine : true;
    const useOnline = onlineAvailable && demoState !== 'offline';
    const forcedTopology = params.get('map') === 'topology' || demoState === 'offline'
      || analyticsMode; // 分析批次默认拓扑地图（离线链路不依赖外网地图）
    try {
      const mountResult = await mapSurface.mount(mapContainer, {
        key: config.tencentMapJsKey,
        online: useOnline,
        forceTopology: forcedTopology,
        stations: model.stations,
        piles: model.piles,
        onStationActivate: activateStationNode,
      });
      // 本轮挂载被更新的重试/销毁取代：容器与状态归新挂载所有，
      // 旧流程放弃后续副作用（不覆盖 modeBadge、不渲染图表）——P1-01。
      if (mountResult.mode === 'superseded') return;
      modeBadge.textContent = mountResult.mode === 'tencent' ? '腾讯地图' : '离线拓扑图';
      if (mountResult.degraded) {
        modeBadge.textContent += '（降级）';
        modeBadge.title = `在线地图不可用，已自动切换拓扑图：${mountResult.reason}`;
      } else {
        modeBadge.removeAttribute('title');
      }
    } catch (error) {
      modeBadge.textContent = '拓扑图';
      renderLocalError(mapContainer, `地图初始化失败：${error?.message ?? '未知原因'}`);
    }

    // 图表（各自独立；失败只影响本面板）
    chartsById = {};
    chartInstances = [];
    const revenueCentsSeries = revenueRangeDays === 30
      ? (model.revenue30dCents ?? [])
      : model.revenue7dCents;
    const revenueLabels = sliceTail(model.revenueSeriesLabels, revenueCentsSeries.length);
    for (const [id, factory] of [
      ['chart-load', () => renderLoadChart(document.getElementById('chart-load'), model)],
      ['chart-states', () => renderStateDonut(document.getElementById('chart-states'), model.metrics.counts)],
      ['chart-revenue', () => renderRevenueTrend(
        document.getElementById('chart-revenue'), revenueCentsSeries, model.updatedAt,
        revenueLabels)],
    ]) {
      try {
        const chart = factory();
        chartsById[id] = chart;
        chartInstances.push(chart);
      } catch (error) {
        renderLocalError(document.getElementById(id), error?.message ?? '图表初始化失败');
      }
    }
    await yieldToMain(); // 三个重渲染阶段之间各让出一帧，避免合成一个超长任务
    await renderOverviewMining(model, chartsById, chartInstances);
    await yieldToMain();
    await renderAnalysisWorkbench(model, chartsById, chartInstances);
    showChartTab(currentChartTab); // 保持当前页签并完成首个 resize
  }

  /* ---- 数据加载与六态呈现（可重试；保留最后有效快照） ---- */

  async function loadDashboard() {
    announce(state.loading().title);
    if (controls) controls.setStatus('loading', '加载中…');
    const hasSnapshot = lastValidModel !== null;
    if (hasSnapshot) {
      showPageState(state.loading(), { blocking: false, freshness: snapshotLabel() });
    } else {
      renderSkeleton(); // 稳定高度骨架，避免地图区跳动
      showPageState(state.loading(), { blocking: false });
    }

    const result = await provider.load(currentQuery);
    if (result.superseded) return; // 迟到响应：由更新的请求拥有 UI，不触碰任何界面状态

    // 演示注入 error：有数据底座但呈现阻断错误分支
    if (demoState === 'error' && result.ok) {
      const failed = state.reject('演示注入：模拟数据服务不可用');
      lastValidModel = null;
      lastValidAt = null;
      hideSkeleton();
      sourceBadge.textContent = '演示状态';
      announce(failed.title);
      showPageState(failed, { blocking: true });
      return;
    }

    if (!result.ok) {
      const failed = state.reject(
        result.error?.message ?? (analyticsMode ? '分析接口加载失败' : 'demo.json 加载失败'));
      sourceBadge.textContent = analyticsMode ? '批次加载失败' : '加载失败';
      if (controls) controls.setStatus('error', result.error?.message ?? '加载失败');
      ensureStationCatalog();
      announce(failed.title);
      if (hasSnapshot) {
        // 快照仍在：非阻断错误横幅，主体继续展示最后有效数据
        showPageState(failed, { blocking: false, freshness: snapshotLabel() });
      } else {
        hideSkeleton();
        showPageState(failed, { blocking: true });
      }
      return;
    }

    const model = result.data;
    // 合法响应先同步覆盖窗口：空结果分支同样依赖它——分享链接首开命中空结果时若跳过此步，
    // “重置”无法恢复全窗口日期（PR #24 评审 P2）。
    if (controls && model?.coverage) controls.setCoverage(model.coverage);
    const isEmpty = result.empty === true || !model
      || !Array.isArray(model.stations) || model.stations.length === 0;
    if (isEmpty || demoState === 'empty') {
      lastValidModel = null;
      lastValidAt = null;
      hideSkeleton();
      sourceBadge.textContent = demoState === 'empty'
        ? '演示状态'
        : analyticsMode ? '窗口无数据' : '暂无数据';
      if (controls) controls.setStatus('empty', '该筛选条件下没有业务记录');
      ensureStationCatalog();
      announce(state.empty().title);
      showPageState(state.empty(), { blocking: true });
      return;
    }

    // 数据有效：记录快照并渲染主体，随后选择非阻断呈现分支
    lastValidModel = model;
    lastValidAt = model.updatedAt ?? null;
    const isOffline =
      demoState === 'offline' || (typeof navigator !== 'undefined' && !navigator.onLine);
    const resolved = state.resolve({ stations: model.stations, fetchedAt: model.updatedAt });
    const isStale = demoState === 'stale' || resolved.isStale || model.stale === true;
    sourceBadge.textContent =
      demoState !== null
        ? '演示状态'
        : analyticsMode
          ? `分析批次 ${result.batchId ?? ''}`
          : isOffline
            ? 'Schema 快照·离线拓扑'
            : 'Schema v0.4 快照';
    if (analyticsMode && result.meta) {
      sourceBadge.title = `${result.meta.source_type} · UTC · 生成于 `
        + `${result.meta.batch_generated_at ?? result.meta.generated_at} · 非实时`;
    }
    if (controls) {
      // 覆盖窗口已在上方（空结果分支之前）写入，这里只处理站点全集与状态；站点下拉使用完整站点全集：过滤后的响应只含单站，不得覆盖选项表；
      // 全集缺失时独立加载完整目录（分享链接首开 / 筛选后刷新的下拉也因此完整）。
      const hasStationFilter = currentQuery.stationId !== null && currentQuery.stationId !== undefined
        && currentQuery.stationId !== '';
      stationCatalog.absorb(model.stations, { filtered: hasStationFilter });
      controls.setStations(stationCatalog.stations ?? model.stations);
      ensureStationCatalog();
      controls.setStatus(
        isStale ? 'stale' : 'ready',
        `批次 ${result.batchId ?? '—'} · 数据截至 ${String(model.updatedAt ?? '').slice(0, 16)}Z`
      );
    }

    await renderContent(model);

    if (isOffline) {
      // 数据仍可用：非阻断横幅，明确业务快照 + 离线拓扑（无重试按钮，网络恢复自动消失）
      const offlineState = { ...state.offline(), action: null };
      showPageState(offlineState, { blocking: false, freshness: snapshotLabel() });
      freshnessEl.textContent = '离线：Schema 快照 + 离线拓扑（自动降级）';
      announce(offlineState.title);
    } else if (isStale) {
      showPageState(state.stale(lastValidAt), { blocking: false, freshness: snapshotLabel() });
      freshnessEl.textContent = `最后更新 ${lastValidAt ?? '—'}（UTC）· 数据可能已过期`;
      announce('数据可能已过期，请刷新');
    } else {
      hidePageState();
      freshnessEl.textContent = analyticsMode
        ? `更新于 ${model.updatedAt}（UTC）· 分析批次 ${result.batchId ?? ''}（模拟数据）`
        : `更新于 ${model.updatedAt}（UTC）· ${result.source} 数据源`;
      announce(`数据已就绪：${model.metrics.totalPiles} 台桩`);
    }
  }

  /* ---- 一次性装配 ---- */

  // 状态区重试按钮：直接重跑 loadDashboard，保留最后有效快照，不做整页刷新
  pageStateEl.querySelector('[data-page-state-action]').addEventListener('click', () => {
    loadDashboard().catch(() => {});
  });

  document.addEventListener('keydown', (event) => {
    if (event.key === 'Escape' && currentDeactivateFocus) {
      currentDeactivateFocus().catch(() => {});
    }
  });

  setupChartTabsOnce();
  setupAnalysisTabsOnce();
  setupWorkspaceNavigation();

  // 营收趋势 7 日/30 日档位：静态按钮只绑一次；切换后按最近有效快照重渲该图
  const revenueRangeGroup = document.querySelector('.chart-range');
  if (revenueRangeGroup) {
    for (const btn of revenueRangeGroup.querySelectorAll('.range-btn')) {
      btn.addEventListener('click', () => {
        const days = Number(btn.dataset.rangeDays);
        if (!Number.isInteger(days) || days === revenueRangeDays) return;
        revenueRangeDays = days;
        for (const other of revenueRangeGroup.querySelectorAll('.range-btn')) {
          const isActive = other === btn;
          other.classList.toggle('is-active', isActive);
          other.setAttribute('aria-pressed', String(isActive));
        }
        if (!lastValidModel) return; // 尚无有效快照（加载失败态），按钮保持但不重渲
        const centsSeries = days === 30
          ? (lastValidModel.revenue30dCents ?? [])
          : lastValidModel.revenue7dCents;
        try {
          const chart = renderRevenueTrend(
            document.getElementById('chart-revenue'), centsSeries, lastValidModel.updatedAt,
            sliceTail(lastValidModel.revenueSeriesLabels, centsSeries.length));
          chartsById['chart-revenue'] = chart;
          if (!chartInstances.includes(chart)) chartInstances.push(chart);
        } catch (error) {
          renderLocalError(document.getElementById('chart-revenue'),
                           error?.message ?? '图表切换失败');
        }
      });
    }
    if (analyticsMode) {
      // 分析批次默认 30 日档：同步按钮激活态（7 日档仍可一键切换）
      for (const other of revenueRangeGroup.querySelectorAll('.range-btn')) {
        const isActive = Number(other.dataset.rangeDays) === revenueRangeDays;
        other.classList.toggle('is-active', isActive);
        other.setAttribute('aria-pressed', String(isActive));
      }
    }
  }

  // 自适应：容器与窗口变化时重排所有活跃图表。
  // 守卫：尺寸未变化不触发 resize——否则 chart.resize() 引起的布局变化会再次触发
  // observer，形成 resize→layout→resize 自激回路，把主线程烧满（VM 软渲染实测 CPU 123%）。
  if (typeof ResizeObserver !== 'undefined') {
    let lastObservedSize = null;
    const observer = new ResizeObserver((entries) => {
      const rect = entries[0]?.contentRect;
      const size = rect ? `${Math.round(rect.width)}x${Math.round(rect.height)}` : null;
      if (size !== null && size === lastObservedSize) return;
      lastObservedSize = size;
      for (const chart of chartInstances) chart.resize();
    });
    observer.observe(document.getElementById('operations-strip'));
  }
  window.addEventListener('resize', () => {
    for (const chart of chartInstances) chart.resize();
  });

  // 分析模式：挂载 Vue 筛选控件（延迟加载模块，demo 模式零开销）
  if (analyticsMode) {
    const { mountAnalyticsControls } = await import('./vue-controls.js');
    controls = mountAnalyticsControls(document.getElementById('analytics-controls'), {
      initial: {
        start: params.get('start'),
        end: params.get('end'),
        stationId: params.get('stationId'),
      },
      onApply: (query) => {
        currentQuery = query;
        syncUrlQuery(query);
        loadDashboard().catch(() => {});
      },
    });
    if (controls) {
      const container = document.getElementById('analytics-controls');
      if (container) container.hidden = false;
    }
  }

  await loadDashboard();
  // 分析服务独立于基础大屏加载；不可用只影响分析区，不阻断站点和桩状态展示。
  // analytics 模式下该服务属于另一条数据链（ML 链路）：结果必须带“独立数据源”标注（评审 P2）。
  const analysisOptions = { independent: analyticsMode };
  analysisProvider.load().then((result) => renderAnalysis(result, analysisOptions)).catch((error) => {
    renderAnalysis({ ok: false, code: 'ANALYSIS_UNAVAILABLE', message: error?.message }, analysisOptions);
  });
}

boot().catch((error) => {
  announce('主屏初始化失败');
  const failed = state.reject(error?.message ?? '未知错误');
  const pageStateEl = document.getElementById('pageState');
  const sourceBadge = document.getElementById('sourceBadge');
  if (sourceBadge) sourceBadge.textContent = '启动失败';
  if (pageStateEl) {
    renderPageState(pageStateEl, failed, { blocking: true });
    document.body.classList.add('page-blocking');
  }
  const shell = document.querySelector('.dashboard-shell');
  if (shell) shell.style.display = 'none';
});

// 数据适配器：把业务快照（或离线演练 fixture）规整为图表/指标可用的视图模型。
// 金额一律整数分，时间 UTC ISO-8601；派生指标全部可追溯到 fixture。
import { mapPileStatus } from './status-map.js';

export { mapPileStatus } from './status-map.js';

/** 整数分 → '¥2,865.40'（手工千分位，无 locale 依赖，杜绝浮点漂移）。 */
export function formatCents(cents) {
  if (!Number.isInteger(cents)) {
    throw new Error(`revenue must be integer cents, got ${cents}`);
  }
  const negative = cents < 0;
  const abs = Math.abs(cents);
  const yuan = Math.floor(abs / 100).toString();
  const fen = (abs % 100).toString().padStart(2, '0');
  const grouped = yuan.replace(/\B(?=(\d{3})+(?!\d))/g, ',');
  return `${negative ? '-' : ''}¥${grouped}.${fen}`;
}

function assertFiniteCoordinate(value, what) {
  if (!Number.isFinite(value)) {
    throw new Error(`${what} must be a finite number, got ${value}`);
  }
}

function assertInteger(value, what) {
  if (!Number.isInteger(value)) {
    throw new Error(`${what} must be an integer, got ${value}`);
  }
}

/**
 * 由 stations/piles/overview 派生视图模型指标（demo 与 analytics 两条链路共用）。
 * 口径：在线 = 非 故障/离线；可用率一位小数；金额为整数分。
 */
export function deriveMetrics(stations, piles, overview) {
  const counts = { idle: 0, reserved: 0, charging: 0, fault: 0, offline: 0, unknown: 0 };
  for (const pile of piles) {
    counts[mapPileStatus(pile.status).protocol] += 1;
  }
  const totalPiles = piles.length;
  const attentionCount = counts.fault + counts.offline;
  const available = totalPiles - attentionCount;
  const availabilityPercent =
    totalPiles === 0 ? 0 : Number(((available / totalPiles) * 100).toFixed(1));
  return {
    totalPiles,
    onlinePiles: available, // 在线 = 非 故障/离线（口径与可用率同源）
    attentionCount,
    availabilityPercent,
    revenueCents: overview?.revenueCents ?? 0,
    avgStationUtilization: overview?.avgStationUtilization,
    counts,
  };
}

/**
 * demo.json fixture → 视图模型。
 * 校验：金额整数、经纬度有限、piles.stationId 必须引用存在的站点。
 */
export function adaptDashboardData(fixture) {
  if (!fixture || typeof fixture !== 'object') {
    throw new Error('fixture must be an object');
  }
  const overview = fixture.overview ?? {};
  assertInteger(overview.revenueCents, 'overview.revenueCents');

  const stationIds = new Set();
  for (const station of fixture.stations ?? []) {
    stationIds.add(station.id);
    assertFiniteCoordinate(station.latitude, `station ${station.id} latitude`);
    assertFiniteCoordinate(station.longitude, `station ${station.id} longitude`);
  }

  const piles = (fixture.piles ?? []).map((pile) => {
    assertInteger(pile.id, `pile ${pile.id} id`);
    if (!stationIds.has(pile.stationId)) {
      throw new Error(`pile ${pile.code} references unknown stationId ${pile.stationId}`);
    }
    return pile;
  });

  return {
    isDemo: fixture.demo === true,
    updatedAt: fixture.updatedAt,
    overview,
    stations: fixture.stations ?? [],
    stationUtilization: fixture.stationUtilization ?? [],
    piles,
    metrics: deriveMetrics(fixture.stations ?? [], piles, overview),
    revenue7dCents: fixture.revenue7dCents ?? [],
    // A-02 近 30 日营收序列（末 7 日与 revenue7dCents 同源；合计与 Qt mockdataset 一致）
    revenue30dCents: fixture.revenue30dCents ?? [],
    demoSeries: fixture.demoSeries,
    analytics: fixture.analytics ?? {},
  };
}

/* ---------- 第二阶段分析链路（/api/dashboard）→ 视图模型 ---------- */

function assertNonEmptyString(value, what) {
  if (typeof value !== 'string' || value.trim() === '') {
    throw new Error(`${what} must be a non-empty string`);
  }
}

function dayLabelFromIso(dateStr) {
  const [, month, day] = dateStr.slice(0, 10).split('-');
  return `${Number(month)}/${Number(day)}`;
}

function hourLabelFromIso(hourIso) {
  const [datePart, timePart] = hourIso.split('T');
  return `${dayLabelFromIso(datePart)} ${timePart.slice(0, 5)}`;
}

/**
 * /api/dashboard 响应 → 视图模型（analytics 模式）。
 * 按 source 校验：必须携带 meta.batch_id/source_type；demo_fixture 不得混入分析链路。
 */
export function adaptAnalyticsPayload(payload) {
  if (!payload || typeof payload !== 'object') {
    throw new Error('analytics payload must be an object');
  }
  if (payload.status !== 'ok' && payload.status !== 'empty') {
    throw new Error(`unexpected analytics status: ${payload.status}`);
  }
  const meta = payload.meta ?? {};
  assertNonEmptyString(meta.batch_id, 'meta.batch_id');
  assertNonEmptyString(meta.source_type, 'meta.source_type');
  if (meta.source_type === 'demo_fixture') {
    throw new Error('demo_fixture must not be served through the analytics provider');
  }
  const data = payload.data ?? {};
  for (const key of ['stations', 'piles', 'revenueDaily', 'loadHourly']) {
    if (!Array.isArray(data[key])) {
      throw new Error(`data.${key} must be an array`);
    }
  }
  const overview = data.overview ?? {};
  assertInteger(overview.revenueCents, 'overview.revenueCents');
  assertInteger(overview.completedOrders ?? 0, 'overview.completedOrders');
  assertInteger(overview.energyWh ?? 0, 'overview.energyWh');

  const stationIds = new Set();
  for (const station of data.stations) {
    stationIds.add(station.id);
    assertFiniteCoordinate(station.latitude, `station ${station.id} latitude`);
    assertFiniteCoordinate(station.longitude, `station ${station.id} longitude`);
  }
  const piles = data.piles.map((pile) => {
    assertInteger(pile.id, `pile ${pile.id} id`);
    if (!stationIds.has(pile.stationId)) {
      throw new Error(`pile ${pile.code} references unknown stationId ${pile.stationId}`);
    }
    return pile;
  });

  const nameById = new Map(data.stations.map((s) => [s.id, s.name]));
  const stationUtilization = (data.stationUtilization ?? []).map((row) => ({
    stationId: row.stationId,
    utilization: row.utilization,
    name: nameById.get(row.stationId) ?? `站点 ${row.stationId}`,
  }));

  const revenueDaily = [...data.revenueDaily].sort((a, b) => (a.date < b.date ? -1 : 1));
  for (const row of revenueDaily) {
    assertInteger(row.revenueCents, `revenueDaily ${row.date} revenueCents`);
  }
  const revenueSeries = revenueDaily.map((row) => ({
    date: row.date,
    label: dayLabelFromIso(row.date),
    cents: row.revenueCents,
  }));
  const loadSeriesPoints = data.loadHourly.map((row) => ({
    label: hourLabelFromIso(row.hourStart),
    loadKw: row.loadKw,
  }));

  return {
    isDemo: false,
    sourceType: meta.source_type,
    batchId: meta.batch_id,
    empty: payload.status === 'empty',
    stale: meta.stale === true,
    // 覆盖窗口（筛选控件 min/max）= available_*；默认窗口（初始值/“重置”）= default_*
    // ——服务端口径：覆盖超 90 天时默认收敛到最新一段；旧批次缺省逐级回退（PR #24 评审 P2）。
    coverage: {
      start: String(meta.available_start ?? meta.data_start ?? '').slice(0, 10),
      endExclusive: String(meta.available_end_exclusive ?? meta.data_end_exclusive ?? '').slice(0, 10),
      defaultStart: String(meta.default_start ?? meta.available_start ?? meta.data_start ?? '').slice(0, 10),
      defaultEndExclusive: String(
        meta.default_end_exclusive ?? meta.available_end_exclusive ?? meta.data_end_exclusive ?? ''
      ).slice(0, 10),
    },
    updatedAt: meta.batch_generated_at ?? meta.generated_at,
    overview,
    stations: data.stations,
    stationUtilization,
    piles,
    metrics: deriveMetrics(data.stations, piles, overview),
    revenueDaily: revenueSeries,
    revenue7dCents: revenueSeries.slice(-7).map((row) => row.cents),
    revenue30dCents: revenueSeries.slice(-30).map((row) => row.cents),
    revenueSeriesLabels: revenueSeries.map((row) => row.label),
    loadSeries: { points: loadSeriesPoints },
    quality: data.quality ?? null,
  };
}

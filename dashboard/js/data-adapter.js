// 数据适配器：把 demo.json（协议口径）规整为图表/指标可用的视图模型。
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

  const counts = { idle: 0, reserved: 0, charging: 0, fault: 0, offline: 0, unknown: 0 };
  const piles = (fixture.piles ?? []).map((pile) => {
    assertInteger(pile.id, `pile ${pile.id} id`);
    if (!stationIds.has(pile.stationId)) {
      throw new Error(`pile ${pile.code} references unknown stationId ${pile.stationId}`);
    }
    counts[mapPileStatus(pile.status).protocol] += 1;
    return pile;
  });

  const totalPiles = piles.length;
  const attentionCount = counts.fault + counts.offline;
  const available = totalPiles - attentionCount;
  const availabilityPercent =
    totalPiles === 0 ? 0 : Number(((available / totalPiles) * 100).toFixed(1));

  return {
    isDemo: fixture.demo === true,
    updatedAt: fixture.updatedAt,
    overview,
    stations: fixture.stations ?? [],
    stationUtilization: fixture.stationUtilization ?? [],
    piles,
    metrics: {
      totalPiles,
      onlinePiles: totalPiles - attentionCount, // 在线 = 非 故障/离线（口径与可用率同源）
      attentionCount,
      availabilityPercent,
      revenueCents: overview.revenueCents,
      avgStationUtilization: overview.avgStationUtilization,
      counts,
    },
    revenue7dCents: fixture.revenue7dCents ?? [],
    demoSeries: fixture.demoSeries,
  };
}

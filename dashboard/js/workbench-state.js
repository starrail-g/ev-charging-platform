// Missing measurements stay missing; a measured zero remains a valid value.
export function metric(value, suffix = '', digits = 0, scale = 1) {
  return value == null || !Number.isFinite(Number(value))
    ? '未提供' : `${(Number(value) * scale).toFixed(digits)}${suffix}`;
}

export function panelMissing(panel, a = {}) {
  const required = {
    users: a.users, equipment: a.equipment, 'user-cohort': a.user_mining,
    'equipment-risk': a.equipment_mining, orders: a.orders, 'order-funnel': a.orders,
    energy: a.energy, 'energy-peaks': a.energy, revenue: a.revenue, stations: a.stations,
    'revenue-regression': a.revenue?.trend, 'station-clusters': a.station_mining,
    service: a.service, 'service-duration': a.service?.duration_buckets,
    'service-control': a.service?.service_control, 'service-data': a.service,
    'overview-health': a.orders?.completion_rate,
    'overview-anomalies': a.equipment_mining, 'overview-clusters': a.station_mining,
    'overview-energy-bands': a.energy,
  };
  return Object.hasOwn(required, panel) && required[panel] == null
    ? '当前数据源未提供此项指标' : null;
}

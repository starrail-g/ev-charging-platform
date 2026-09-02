// 统一地图表面：真实地图优先（腾讯地图，密钥可选）、离线拓扑兜底。
// 模式选择与坐标投影为纯函数（无浏览器环境可测）；MapSurface 负责挂载与自动降级。

const REASONS = Object.freeze({
  NONE: null,
  SDK_TIMEOUT: 'sdk-load-timeout',
  SDK_LOAD_FAILED: 'sdk-load-failed',
  MAP_INIT_FAILED: 'map-init-failed',
  RENDERER_FAILED: 'renderer-failed',
});

// 与 Qt StationTopologyWidget priority 一致：Fault > Offline > Charging > Reserved > Idle
const STATE_PRIORITY = ['fault', 'offline', 'charging', 'reserved', 'idle'];

/**
 * 站点“最需关注状态”：桩明细按优先级取最严重者；无桩/无明细 → unknown。
 * Qt/Web 双端共用同一语义，保证拓扑与真实地图着色一致。
 */
export function resolveStationState(station, pilesByStation) {
  const own = pilesByStation.get(station.id) ?? [];
  if (own.length === 0) return 'unknown';
  for (const protocol of STATE_PRIORITY) {
    if (own.some((pile) => pile.status === protocol)) return protocol;
  }
  return 'unknown';
}

/**
 * 只依据 key / online / forceTopology 决定渲染模式。
 * 无开发密钥、断网或显式 forceTopology → 离线拓扑。
 */
export function chooseMapMode({ key, online, forceTopology }) {
  if (forceTopology || !key || !online) return 'topology';
  return 'tencent';
}

/**
 * 经纬度包围盒 → 等比线性投影到 [padding, size-padding]。
 * 单一站点置于画布中心；绝不产生 NaN/Infinity。
 */
export function projectStations(stations, { width, height, padding }) {
  if (stations.length === 0) return [];
  const lons = stations.map((station) => station.longitude);
  const lats = stations.map((station) => station.latitude);
  const minLon = Math.min(...lons);
  const maxLon = Math.max(...lons);
  const minLat = Math.min(...lats);
  const maxLat = Math.max(...lats);
  const lonSpan = maxLon - minLon;
  const latSpan = maxLat - minLat;
  const innerWidth = Math.max(width - 2 * padding, 1);
  const innerHeight = Math.max(height - 2 * padding, 1);

  const centerX = width / 2;
  const centerY = height / 2;

  return stations.map((station) => {
    let x = centerX;
    let y = centerY;
    if (lonSpan > 1e-9) {
      x = padding + ((station.longitude - minLon) / lonSpan) * innerWidth;
    }
    if (latSpan > 1e-9) {
      // SVG y 轴向下：北纬大 → 画布上方
      y = padding + ((maxLat - station.latitude) / latSpan) * innerHeight;
    }
    return { x, y, station };
  });
}

function withTimeout(promise, timeoutMs, reason) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error(reason)), timeoutMs);
    promise.then(
      (value) => {
        clearTimeout(timer);
        resolve(value);
      },
      (error) => {
        clearTimeout(timer);
        reject(error);
      }
    );
  });
}

/**
 * 表面：尝试在线渲染器（超时/异常自动降级为拓扑），
 * 失败原因只暴露枚举值，不含密钥或完整 URL。
 */
export class MapSurface {
  constructor({ onlineRenderer, topologyRenderer, timeoutMs = 5000 }) {
    this._onlineRenderer = onlineRenderer;
    this._topologyRenderer = topologyRenderer;
    this._timeoutMs = timeoutMs;
    this._active = null;
  }

  async mount(container, config) {
    const mode = chooseMapMode(config);
    if (mode === 'topology') {
      this._active = this._topologyRenderer;
      await this._topologyRenderer.mount(container, config);
      return { mode: 'topology', degraded: false, reason: REASONS.NONE };
    }
    try {
      await withTimeout(
        this._onlineRenderer.mount(container, config),
        this._timeoutMs,
        REASONS.SDK_TIMEOUT
      );
      this._active = this._onlineRenderer;
      return { mode: 'tencent', degraded: false, reason: REASONS.NONE };
    } catch (error) {
      container.innerHTML = ''; // 清空半成品容器后立即挂载拓扑图
      const reason =
        error && typeof error.message === 'string' && error.message.startsWith('sdk-')
          ? error.message
          : REASONS.RENDERER_FAILED;
      this._active = this._topologyRenderer;
      await this._topologyRenderer.mount(container, config);
      return { mode: 'topology', degraded: true, reason };
    }
  }

  /** 高亮站点（告警联动）；委托给当前激活的渲染器。 */
  async focusStation(stationId) {
    if (this._active && typeof this._active.focusStation === 'function') {
      await this._active.focusStation(stationId);
    }
  }

  async destroy() {
    if (this._active && typeof this._active.destroy === 'function') {
      await this._active.destroy();
    }
    this._active = null;
  }
}

export { REASONS };

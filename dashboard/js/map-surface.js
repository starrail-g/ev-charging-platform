// 统一地图表面：真实地图优先（腾讯地图，密钥可选）、离线拓扑兜底。
// 模式选择与坐标投影为纯函数（无浏览器环境可测）；MapSurface 负责挂载与自动降级。

const REASONS = Object.freeze({
  NONE: null,
  SDK_TIMEOUT: 'sdk-load-timeout',
  SDK_LOAD_FAILED: 'sdk-load-failed',
  MAP_INIT_FAILED: 'map-init-failed',
  RENDERER_FAILED: 'renderer-failed',
});

// 挂载被更新的挂载/销毁取代时的返回结果：调用方应放弃本轮后续副作用
// （不更新 _active、不清容器、不降级——容器与 _active 归新挂载所有）。
const SUPERSEDED = Object.freeze({
  mode: 'superseded',
  degraded: false,
  reason: REASONS.NONE,
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

function normalizeReason(error) {
  return error && typeof error.message === 'string' && error.message.startsWith('sdk-')
    ? error.message
    : REASONS.RENDERER_FAILED;
}

/**
 * 表面：尝试在线渲染器（超时/异常自动降级为拓扑），
 * 失败原因只暴露枚举值，不含密钥或完整 URL。
 *
 * 容器生命周期由 MapSurface 单点管理：超时或失败后先让在线渲染器
 * 释放自身引用（destroy），再清空共享容器，最后挂载拓扑；渲染器
 * 一律不得自行执行 container.innerHTML = ''，以免晚到的在线完成
 * 覆盖已就位的拓扑图（竞态）。取消通过 AbortSignal 传达，渲染器在
 * SDK 返回后、初始化前检查 aborted。
 */
export class MapSurface {
  constructor({ onlineRenderer, topologyRenderer, timeoutMs = 5000 }) {
    this._onlineRenderer = onlineRenderer;
    this._topologyRenderer = topologyRenderer;
    this._timeoutMs = timeoutMs;
    this._active = null;
    // 挂载代次：每次 mount()/destroy() 递增；异步 Promise 完成后只有
    // 代次仍匹配才允许更新 _active / 触碰容器（评审 P1-01 竞态修复）。
    this._generation = 0;
    // 在途在线挂载的 AbortController；新一轮挂载开始时中止它，
    // 让尚未完成 SDK 初始化的旧在线渲染器放弃写入共享容器。
    this._onlineController = null;
  }

  _isCurrent(generation) {
    return generation === this._generation;
  }

  async mount(container, config) {
    // 本轮挂载代次：递增即令所有在途旧挂载失效（P1-01）。
    const generation = ++this._generation;

    // 中止上一轮在途在线挂载：SDK 尚未完成初始化时会因 abort 放弃写容器；
    // 其 catch/完成路径经代次检查后会静默退出，不会降级接管本轮。
    if (this._onlineController) {
      this._onlineController.abort();
      this._onlineController = null;
    }

    // 幂等：重复挂载（重试）先释放上一轮渲染器并清空共享容器，
    // 容器生命周期归 MapSurface 单点管理。
    if (this._active && typeof this._active.destroy === 'function') {
      await this._active.destroy({ preserveContainer: true });
      if (!this._isCurrent(generation)) return SUPERSEDED;
    }
    this._active = null;
    container.replaceChildren?.();
    if (!container.replaceChildren) container.innerHTML = '';

    const mode = chooseMapMode(config);
    if (mode === 'topology') {
      this._active = this._topologyRenderer;
      await this._topologyRenderer.mount(container, config);
      if (!this._isCurrent(generation)) return SUPERSEDED;
      return { mode: 'topology', degraded: false, reason: REASONS.NONE };
    }
    const controller = new AbortController();
    this._onlineController = controller;
    const timer = setTimeout(() => controller.abort(REASONS.SDK_TIMEOUT), this._timeoutMs);
    try {
      const mountPromise = this._onlineRenderer.mount(container, {
        ...config,
        signal: controller.signal,
      });
      await this._untilSettledOrAborted(mountPromise, controller);
      // 旧在线请求晚到时不得覆盖新挂载结果：代次不匹配则放弃本轮声明
      if (!this._isCurrent(generation)) return SUPERSEDED;
      this._active = this._onlineRenderer;
      return { mode: 'tencent', degraded: false, reason: REASONS.NONE };
    } catch (error) {
      // 被更新的挂载取代：不降级、不触碰容器（容器/_active 归新挂载）
      if (!this._isCurrent(generation)) return SUPERSEDED;
      controller.abort();
      // 先放弃引用再销毁，避免与并发新挂载对同一渲染器双重 destroy
      this._active = null;
      await this._onlineRenderer.destroy?.({ preserveContainer: true });
      if (!this._isCurrent(generation)) return SUPERSEDED;
      container.replaceChildren?.();
      if (!container.replaceChildren) container.innerHTML = '';
      this._active = this._topologyRenderer;
      await this._topologyRenderer.mount(container, config);
      return { mode: 'topology', degraded: true, reason: normalizeReason(error) };
    } finally {
      clearTimeout(timer);
      if (this._onlineController === controller) this._onlineController = null;
    }
  }

  /**
   * 等待在线挂载完成或中止信号触发；中止立即以 sdk 原因拒绝，
   * 不让 MapSurface 卡在永不返回的在线 Promise 上。
   */
  _untilSettledOrAborted(promise, controller) {
    return new Promise((resolve, reject) => {
      let settled = false;
      const onAbort = () => {
        if (settled) return;
        settled = true;
        controller.signal.removeEventListener('abort', onAbort);
        reject(new Error(controller.signal.reason ?? REASONS.SDK_TIMEOUT));
      };
      controller.signal.addEventListener('abort', onAbort);
      promise.then(
        (value) => {
          if (settled) return;
          settled = true;
          controller.signal.removeEventListener('abort', onAbort);
          if (controller.signal.aborted) {
            reject(new Error(controller.signal.reason ?? REASONS.SDK_TIMEOUT));
          } else {
            resolve(value);
          }
        },
        (error) => {
          if (settled) return;
          settled = true;
          controller.signal.removeEventListener('abort', onAbort);
          reject(error);
        }
      );
    });
  }

  /** 高亮站点（告警联动）；委托给当前激活的渲染器。 */
  async focusStation(stationId) {
    if (this._active && typeof this._active.focusStation === 'function') {
      await this._active.focusStation(stationId);
    }
  }

  /** 退出异常聚焦（清高亮/降权）；委托给当前激活的渲染器。 */
  async clearFocus() {
    if (this._active && typeof this._active.clearFocus === 'function') {
      await this._active.clearFocus();
    }
  }

  async destroy() {
    // 销毁令所有在途挂载失效：其完成/失败路径不得再触碰容器或 _active
    this._generation += 1;
    if (this._onlineController) {
      this._onlineController.abort();
      this._onlineController = null;
    }
    if (this._active && typeof this._active.destroy === 'function') {
      await this._active.destroy();
    }
    this._active = null;
  }
}

export { REASONS };

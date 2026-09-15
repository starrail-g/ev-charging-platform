// 站点下拉全集目录：独立于当前筛选加载"完整站点表"。
// 动机：分享链接（?source=analytics&stationId=101）或筛选后刷新时，首个响应即为
// 单站过滤态（仅含本站），不能作为下拉选项；全集缺失时必须独立发起一次未过滤请求。
// 契约：①未过滤响应 absorb 直接成为全集；②过滤响应不得覆盖既有全集；
// ③ensure 并发去重；④任何失败不清空、不阻断主视图，后续可重试。
export function createStationCatalog({ load, onCatalog = () => {} } = {}) {
  if (typeof load !== 'function') {
    throw new Error('station catalog requires a load function');
  }
  let cache = null;
  let inflight = null;

  function publish(stations) {
    cache = stations;
    onCatalog(stations);
    return stations;
  }

  return {
    get stations() {
      return cache;
    },
    /** 吸收一次主视图响应中的站点数组：仅未过滤（全集）响应可更新目录。 */
    absorb(stations, { filtered = false } = {}) {
      if (!filtered && Array.isArray(stations) && stations.length > 0) {
        publish(stations);
      }
      return cache;
    },
    /** 确保完整目录已加载：缓存命中直接返回；并发去重；失败解析为当前缓存（不抛出）。 */
    ensure() {
      if (cache) return Promise.resolve(cache);
      if (inflight) return inflight;
      const attempt = load({})
        .then((result) => {
          if (inflight === attempt) inflight = null;
          const stations = result && result.ok && !result.superseded && result.data
            ? result.data.stations
            : null;
          if (Array.isArray(stations) && stations.length > 0) {
            return publish(stations);
          }
          return cache;
        })
        .catch(() => {
          if (inflight === attempt) inflight = null;
          return cache;
        });
      inflight = attempt;
      return attempt;
    },
  };
}

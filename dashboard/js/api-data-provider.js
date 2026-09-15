// 分析链路数据提供者（第二阶段）：GET /api/dashboard，取数 + 契约校验 + 适配视图模型。
// 时序保障：请求序号 + AbortController —— 旧筛选的迟到响应不得覆盖新筛选（映射为 superseded，
// 调用方必须静默忽略，不触碰任何 UI）。错误携带结构化 code（invalid_date/out_of_coverage/...）。
import { adaptAnalyticsPayload } from './data-adapter.js';

export class ApiDataError extends Error {
  constructor(message, { code = null, httpStatus = null } = {}) {
    super(message);
    this.name = 'ApiDataError';
    this.code = code;
    this.httpStatus = httpStatus;
  }
}

export class ApiDataProvider {
  constructor({ baseUrl = '.', fetchImpl = null } = {}) {
    this.baseUrl = baseUrl.replace(/\/$/, '');
    this.fetchImpl = fetchImpl ?? ((...args) => fetch(...args));
    this._seq = 0;
    this._controller = null;
  }

  buildUrl(query = {}) {
    const params = new URLSearchParams();
    if (query.start) params.set('start', query.start);
    if (query.end) params.set('end', query.end);
    if (query.stationId !== null && query.stationId !== undefined && query.stationId !== '') {
      params.set('station_id', String(query.stationId));
    }
    const qs = params.toString();
    return `${this.baseUrl}/api/dashboard${qs ? `?${qs}` : ''}`;
  }

  /**
   * @returns {Promise<{ok:boolean, superseded?:boolean, source:'api', data?:object,
   *                    empty?:boolean, meta?:object, batchId?:string,
   *                    fetchedAt?:string, error?:Error}>}
   */
  async load(query = {}) {
    const seq = ++this._seq;
    if (this._controller) this._controller.abort();
    const controller = new AbortController();
    this._controller = controller;

    try {
      const response = await this.fetchImpl(this.buildUrl(query), {
        signal: controller.signal,
        cache: 'no-store',
        headers: { Accept: 'application/json' },
      });
      let body = null;
      let jsonError = null;
      try {
        body = await response.json();
      } catch (error) {
        jsonError = error;
      }
      if (seq !== this._seq) {
        return { ok: false, superseded: true, source: 'api' };
      }
      if (jsonError || !body || typeof body !== 'object') {
        throw new ApiDataError('响应不是合法 JSON', { httpStatus: response.status });
      }
      if (!response.ok || body.status === 'error') {
        const detail = body.error ?? {};
        throw new ApiDataError(detail.message ?? `HTTP ${response.status}`, {
          code: detail.code ?? null,
          httpStatus: response.status,
        });
      }
      const model = adaptAnalyticsPayload(body); // 契约违例 → 抛错（不把脏数据画到屏上）
      return {
        ok: true,
        source: 'api',
        fetchedAt: new Date().toISOString(),
        empty: body.status === 'empty',
        meta: body.meta,
        batchId: body.meta.batch_id,
        data: model,
      };
    } catch (error) {
      if (error?.name === 'AbortError' || seq !== this._seq) {
        return { ok: false, superseded: true, source: 'api' };
      }
      return { ok: false, source: 'api', error };
    }
  }
}

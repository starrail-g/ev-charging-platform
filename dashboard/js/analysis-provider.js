// 分析服务适配器：只读取 Flask ADS 产物，不生成或缓存业务数据。
export class AnalysisProvider {
  constructor(baseUrl = '') {
    this.baseUrl = String(baseUrl || '').replace(/\/$/, '');
  }

  async load() {
    if (!this.baseUrl) {
      return { ok: false, code: 'ANALYSIS_NOT_CONFIGURED', message: '分析服务地址未配置' };
    }
    try {
      const [health, forecast, recommendations, alerts] = await Promise.all([
        this.#get('/api/v1/analysis/health'),
        this.#post('/api/v1/analysis/forecast', { horizons_hours: [1, 6, 24] }),
        this.#get('/api/v1/analysis/recommendations'),
        this.#get('/api/v1/analysis/alerts'),
      ]);
      return { ok: true, health, forecast, recommendations, alerts };
    } catch (error) {
      return { ok: false, code: 'ANALYSIS_UNAVAILABLE', message: error?.message || '分析服务不可用' };
    }
  }

  async #get(path) {
    const response = await fetch(`${this.baseUrl}${path}`, { cache: 'no-store' });
    if (!response.ok) throw new Error(`analysis GET ${path}: HTTP ${response.status}`);
    return response.json();
  }

  async #post(path, body) {
    const response = await fetch(`${this.baseUrl}${path}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
      cache: 'no-store',
    });
    if (!response.ok) throw new Error(`analysis POST ${path}: HTTP ${response.status}`);
    return response.json();
  }
}

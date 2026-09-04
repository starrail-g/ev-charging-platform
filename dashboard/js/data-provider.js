// 演示数据提供者：只负责取数与适配，不操作 DOM。
// 返回统一结果 { ok, source, fetchedAt, data } 或 { ok:false, source, error }。
import { adaptDashboardData } from './data-adapter.js';

export class DemoDataProvider {
  async load() {
    try {
      const response = await fetch('./data/demo.json', { cache: 'no-store' });
      if (!response.ok) {
        throw new Error(`demo.json fetch failed: HTTP ${response.status}`);
      }
      const fixture = await response.json();
      return {
        ok: true,
        source: 'demo',
        fetchedAt: new Date().toISOString(),
        data: adaptDashboardData(fixture),
      };
    } catch (error) {
      return { ok: false, source: 'demo', error };
    }
  }
}

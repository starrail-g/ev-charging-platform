// Vue 3 筛选控件（本地 ESM，无构建工具）：日期窗口 + 站点筛选 + 链路状态。
// 只拥有自己的子树（#analytics-controls）；既有 DOM 代码不写这些节点。
// 状态文案由调用方通过 setStatus 驱动（loading/ready/empty/stale/error），本组件不猜测业务。
import { createApp, computed, reactive } from '../vendor/vue.esm-browser.prod.js';

const DATE_RE = /^\d{4}-\d{2}-\d{2}$/;

export function mountAnalyticsControls(container, options = {}) {
  if (!container || typeof container.appendChild !== 'function') {
    return null;
  }
  const { initial = {}, onApply = () => {} } = options;

  const state = reactive({
    start: initial.start && DATE_RE.test(initial.start) ? initial.start : '',
    end: initial.end && DATE_RE.test(initial.end) ? initial.end : '',
    stationId: initial.stationId ?? '',
    stations: [],
    coverage: { start: '', endExclusive: '', defaultStart: '', defaultEndExclusive: '' },
    status: 'idle',
    message: '等待批次数据…',
  });

  function currentQuery() {
    return {
      start: state.start || null,
      end: state.end || null,
      stationId: state.stationId === '' ? null : Number(state.stationId),
    };
  }

  const app = createApp({
    setup() {
      const canApply = computed(
        () => DATE_RE.test(state.start) && DATE_RE.test(state.end) && state.start < state.end
      );
      const apply = () => {
        if (!canApply.value) return;
        onApply(currentQuery());
      };
      const reset = () => {
        // 回到默认筛选：默认窗口（服务端口径，覆盖超 90 天时为最新 ≤90 天一段）+ 全部站点
        state.start = state.coverage.defaultStart || state.coverage.start;
        state.end = state.coverage.defaultEndExclusive || state.coverage.endExclusive;
        state.stationId = '';
        apply();
      };
      return { state, canApply, apply, reset };
    },
    template: `
      <div class="ctl" role="group" aria-label="分析窗口筛选">
        <label class="ctl-field">起
          <input type="date" v-model="state.start" :min="state.coverage.start"
                 :max="state.coverage.endExclusive" aria-label="窗口起（含）">
        </label>
        <label class="ctl-field">止
          <input type="date" v-model="state.end" :min="state.coverage.start"
                 :max="state.coverage.endExclusive" aria-label="窗口止（不含）">
        </label>
        <label class="ctl-field">站点
          <select v-model="state.stationId" aria-label="站点筛选">
            <option value="">全部站点</option>
            <option v-for="s in state.stations" :key="s.id" :value="String(s.id)">
              {{ s.name }}
            </option>
          </select>
        </label>
        <button type="button" class="ctl-btn" :disabled="!canApply || state.status === 'loading'"
                @click="apply">应用</button>
        <button type="button" class="ctl-btn ctl-btn-ghost" @click="reset">重置</button>
        <span class="ctl-status" :data-status="state.status" aria-live="polite">{{ state.message }}</span>
      </div>`,
  });
  app.mount(container);

  return {
    setCoverage({ start, endExclusive, defaultStart, defaultEndExclusive } = {}) {
      if (start) state.coverage.start = start;
      if (endExclusive) state.coverage.endExclusive = endExclusive;
      if (defaultStart) state.coverage.defaultStart = defaultStart;
      else if (!state.coverage.defaultStart) state.coverage.defaultStart = state.coverage.start;
      if (defaultEndExclusive) state.coverage.defaultEndExclusive = defaultEndExclusive;
      else if (!state.coverage.defaultEndExclusive) state.coverage.defaultEndExclusive = state.coverage.endExclusive;
      if (!state.start && state.coverage.defaultStart) state.start = state.coverage.defaultStart;
      if (!state.end && state.coverage.defaultEndExclusive) state.end = state.coverage.defaultEndExclusive;
    },
    setStations(stations) {
      state.stations = Array.isArray(stations) ? stations : [];
    },
    setStatus(status, message = '') {
      state.status = status;
      if (message) state.message = message;
    },
    getQuery: currentQuery,
    getState: () => state,
    unmount() {
      app.unmount();
    },
  };
}

// 页面级状态呈现模型：把 dashboard-state 的状态对象规整成
// { icon, title, description, action }，供状态区与无障碍播报渲染；
// 纯函数，无 DOM 依赖，可在 node:test 中直接验证。
// 页面状态图标与桩状态图标分开：桩状态用 status-icons.js（颜色编码），
// 页面状态用这里的轮廓图形（跟随 currentColor）。

const FALLBACK = { icon: 'activity', title: '未知状态', description: '', action: null };

/** 状态 → 呈现元数据；只有带动作的状态才返回按钮文案，否则 action 为 null。 */
export function statePresentation(state) {
  const source = state ?? {};
  const detail = typeof source.detail === 'string' && source.detail.length > 0
    ? source.detail
    : null;
  return {
    icon: source.icon ?? FALLBACK.icon,
    title: source.title ?? source.kind ?? FALLBACK.title,
    description: detail ?? source.description ?? FALLBACK.description,
    action: source.action ?? null,
  };
}

const STROKE = 'fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"';

function wrap(inner) {
  return `<svg class="page-state-icon" viewBox="0 0 16 16" aria-hidden="true" focusable="false">${inner}</svg>`;
}

/** 页面状态图标（与桩状态语义分离的轮廓图形）。 */
export const PAGE_ICONS = {
  loader: wrap(`<path d="M8 1.8v2.6" ${STROKE}/><path d="M8 11.6v2.6" ${STROKE}/><path d="M1.8 8h2.6" ${STROKE}/><path d="M11.6 8h2.6" ${STROKE}/><path d="M3.6 3.6l1.9 1.9" ${STROKE}/><path d="M10.5 10.5l1.9 1.9" ${STROKE}/><path d="M12.4 3.6l-1.9 1.9" ${STROKE}/><path d="M5.5 10.5l-1.9 1.9" ${STROKE}/>`),
  activity: wrap(`<path d="M1.6 10.2l3.1-4 2.6 3.1 3.6-5.4 3.5 4.9" ${STROKE}/><path d="M1.6 13.6h12.8" ${STROKE}/>`),
  'alert-triangle': wrap(`<path d="M8 2.3 14.3 13.2H1.7Z" ${STROKE}/><path d="M8 6.2v3.1" ${STROKE}/><circle cx="8" cy="11.4" r="0.8" fill="currentColor" stroke="none"/>`),
  inbox: wrap(`<path d="M2.4 8.3 3.8 3.2h8.4l1.4 5.1v4.4H2.4Z" ${STROKE}/><path d="M10.6 6.6h2.4v3.1M3 6.6h2.6M6.6 9.6h2.8" ${STROKE}/>`),
  'wifi-off': wrap(`<path d="M1.9 6.1a9.6 9.6 0 0 1 4.3-2M6.9 3.1c2.4-.1 4.8.8 7.2 3M2.9 9.2a6.2 6.2 0 0 1 3.1-1.7M9.9 7.5a6 6 0 0 1 3.2 1.7M5.6 12a3.1 3.1 0 0 1 4.8 0" ${STROKE}/><path d="M1.4 1.4l13.2 13.2" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/>`),
  clock: wrap(`<circle cx="8" cy="8" r="5.6" ${STROKE}/><path d="M8 4.9V8l2.1 1.4" ${STROKE}/>`),
  cpu: wrap(`<rect x="3.6" y="3.6" width="8.8" height="8.8" rx="1.2" ${STROKE}/><path d="M6.4 6.4h3.2v3.2H6.4Zm-2.8 2H1.7m0-2h1.9M5.9 3.6V1.7m4.2 1.9V1.7M12.4 6.4h1.9m0 2h-1.9M10.1 12.4v1.9M5.9 12.4v1.9" ${STROKE}/>`),
  info: wrap(`<circle cx="8" cy="8" r="5.6" ${STROKE}/><path d="M8 7.2v3.6M8 4.9v.3" ${STROKE}/>`),
};

/** 渲染整个页面状态区；返回更新后的状态容器元素（无状态时调用方可隐藏）。
 *  blocking：阻断主内容（loading 初次 / empty / 无快照 error）；非阻断以横幅形式叠在主体上方。 */
export function renderPageState(
  container,
  state,
  { freshness = null, demo = false, blocking = false } = {}
) {
  const presentation = statePresentation(state);
  container.hidden = false;
  const icon = container.querySelector('[data-page-state-icon]');
  if (icon) icon.innerHTML = PAGE_ICONS[presentation.icon] ?? PAGE_ICONS.info;
  const title = container.querySelector('[data-page-state-title]');
  if (title) title.textContent = presentation.title;
  const description = container.querySelector('[data-page-state-description]');
  if (description) {
    description.textContent = presentation.description;
    description.dataset.demo = demo ? 'true' : 'false';
  }
  const freshnessEl = container.querySelector('[data-page-state-freshness]');
  if (freshnessEl) {
    freshnessEl.hidden = !freshness;
    if (freshness) freshnessEl.textContent = freshness;
  }
  const action = container.querySelector('[data-page-state-action]');
  if (action) {
    action.hidden = !presentation.action;
    action.textContent = presentation.action ?? '';
  }
  container.dataset.blocking = blocking ? 'true' : 'false';
  return container;
}

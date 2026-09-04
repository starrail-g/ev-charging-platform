// 自制状态图标（SVG，stroke=currentColor，随状态色渲染）。
// 形状语义对应 design spec §8.1：颜色/文字/图标三编码，任何状态不依赖单一通道。
// idle 空心圆+对勾 / reserved 时钟+虚线外环 / charging 闪电 / fault 警告三角 /
// offline 断连圆+斜杠 / unknown 问号菱形。不引入任何图标库。

const COMMON = 'fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"';

function wrap(inner) {
  return `<svg class="status-icon" viewBox="0 0 16 16" aria-hidden="true" focusable="false">${inner}</svg>`;
}

const PATHS = {
  idle: `<circle cx="8" cy="8" r="5.4" ${COMMON}/><path d="M5.5 8.4l1.8 1.8 3.4-4.1" ${COMMON}/>`,
  reserved: `<circle cx="8" cy="8" r="5.4" stroke="currentColor" stroke-width="1.4" stroke-dasharray="2.4 1.7"/><path d="M8 5v3.2l2.2 1.3" ${COMMON}/>`,
  charging: `<path d="M8.9 1.7 4.4 8.9h2.7l-.7 5.4 4.9-6.8H8.5z" fill="currentColor" stroke="none"/>`,
  fault: `<path d="M8 2.1 14.6 13.2H1.4Z" ${COMMON}/><path d="M8 6.3v3.2" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/><circle cx="8" cy="11.6" r="0.7" fill="currentColor" stroke="none"/>`,
  offline: `<circle cx="8" cy="8" r="5.3" ${COMMON}/><path d="M4.6 4.6l6.8 6.8" ${COMMON}/>`,
  unknown: `<path d="M8 2.3 13.7 8 8 13.7 2.3 8Z" ${COMMON}/><path d="M6.4 7a1.7 1.7 0 1 1 2.9 1.2c-.6.5-.7.9-.6 1.5" ${COMMON}/><circle cx="8.7" cy="11.5" r="0.7" fill="currentColor" stroke="none"/>`,
};

/** 返回状态对应的内联 SVG 字符串（颜色继承 currentColor）。 */
export function statusIconSvg(protocol) {
  return wrap(PATHS[protocol] ?? PATHS.unknown);
}

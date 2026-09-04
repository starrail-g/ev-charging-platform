// 统一五态映射：唯一入口，读取生成的主题令牌（夜班态语义色）。
// 与 Qt 端（StatusTag / StatusPulseWidget）语义一致：protocol 五态 + unknown 兜底。
import { themeTokens } from './generated/theme-tokens.js';

export const STATUS_META = Object.freeze({
  idle: { label: '空闲', icon: 'check-circle', motion: 'none' },
  reserved: { label: '已预约', icon: 'clock-dashed', motion: 'none' },
  charging: { label: '充电中', icon: 'bolt-dot', motion: 'pulse' },
  fault: { label: '故障', icon: 'warning-triangle', motion: 'attention' },
  offline: { label: '离线', icon: 'link-off', motion: 'none' },
  unknown: { label: '未知', icon: 'question-diamond', motion: 'none' },
});

const NIGHT_STATES = themeTokens.themes.night.states;

export function stateColor(value) {
  return NIGHT_STATES[value] ?? NIGHT_STATES.unknown;
}

/**
 * 任意输入 → { protocol, label, icon, motion, color }。
 * 未知/未来状态安全降级为 unknown，绝不抛异常。
 */
export function mapPileStatus(value) {
  const protocol = STATUS_META[value] ? value : 'unknown';
  return {
    protocol,
    ...STATUS_META[protocol],
    color: stateColor(protocol),
  };
}

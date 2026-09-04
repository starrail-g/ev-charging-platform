#ifndef AURORABACKDROP_H
#define AURORABACKDROP_H

#include <QWidget>

class QVariantAnimation;

// 全窗口背景氛围层（统一 UI 追加设计：日班"浅青蓝呼吸"，与 Web 夜班极光同构）：
//   - 绘制日班背景色 + 两个大半径径向光斑（聚焦蓝/能源绿，极低透明度），
//     缓慢"呼吸"起伏；光斑只透出在页面根与容器间隙（面板 surface 不透明，
//     文字对比度不受影响，spec §4.3 减少动态约束）。
//   - 动效曲线与 Web 极光完全同构（spec §5.6 四段式闭合循环）：
//     0% → 45% 亮起 → 90% 回到起始 → 100% 保持停顿；周期读生成令牌
//     kMotionAuroraMs（design-tokens.json motion.aurora = 11000ms）。
//   - 减少动态效果：构造时读 Theme::motionEnabled()；关闭后静止为固定
//     低亮度氛围光（与 Web prefers-reduced-motion 对称），不呼吸。
class AuroraBackdrop : public QWidget
{
public:
    explicit AuroraBackdrop(QWidget *parent = nullptr);

    void setMotionEnabled(bool enabled);
    bool isAnimationRunning() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void updateAnimation();
    // 光斑基准透明度随动画在 [kMinGlowAlpha, kMaxGlowAlpha] 起伏；
    // 静止（减少动效）时固定在中间值，保持氛围但不运动。
    QVariantAnimation *m_animation = nullptr;
    double m_glowAlpha = 0.0;
    bool m_motionEnabled = true;
};

#endif // AURORABACKDROP_H

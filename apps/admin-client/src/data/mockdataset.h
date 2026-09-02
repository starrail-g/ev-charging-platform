#ifndef MOCKDATASET_H
#define MOCKDATASET_H

#include "models/adminmodels.h"

// 第一阶段 Mock 三组演示数据（9/3 大屏 demo JSON 同一口径）：
//   Normal —— 确定、可重复、可追溯的正常数据
//   Empty  —— 空数据（列表为空 / 指标为零，ok=true）
//   Error  —— 模拟接口失败（ok=false + error 文案，调用方据 ok 分支展示错误态）
// 金额一律整数分；时间 UTC ISO-8601；桩状态协议五态。
// 9/4 与 B 字段评审后对齐最终 schema。
namespace ev {
namespace mockdata {

enum class DataMode {
    Normal,
    Empty,
    Error,
};

// 通用结果包装：ok=false 表示接口失败（Error 模式），
// 调用方必须按 ok 分支展示错误态，不能只依赖列表长度。
template <typename T>
struct MockResult {
    bool ok = true;
    QString error;
    QList<T> items;
};

MockResult<PileInfo> piles(DataMode mode);
MockResult<StationInfo> stations(DataMode mode);
MockResult<UserInfo> users(DataMode mode);
OverviewResult overview(DataMode mode);

} // namespace mockdata
} // namespace ev

#endif // MOCKDATASET_H

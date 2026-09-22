// ==========================================================
// network/UpdateChecker.h
//   软件更新检查模块（Organic Chemistry 接入）。
//
//   启动时异步 GET iisaacbeats.cn/api/update/check，服务端返回 JSON
//   告知是否有新版本。若有，在主线程弹窗提示用户；用户可选择
//   "立即下载"（打开浏览器）或"稍后提醒"。
//
//   线程安全：
//     · HTTP 请求在后台线程执行；
//     · 回调通过 MessageManager::callAsync 切回主线程；
//     · 回调中的 UI 操作在主线程安全。
// ==========================================================

#pragma once

#include <JuceHeader.h>

#include <functional>

namespace organic {
namespace network {

// 服务端返回的更新信息
struct UpdateInfo {
  bool has_update = false;      // 是否有新版本
  juce::String latest_version;  // 最新版本号，如 "0.10.0"
  juce::String download_url;    // 下载链接
  juce::String changelog;       // 更新日志（纯文本）
  bool force_update = false;    // 强制更新（严重 bug 修复）
};

// 异步检查更新，完成后在主线程回调。
//
// 参数：
//   product          产品标识，如 "organic-chemistry"
//   current_version  当前软件版本字符串，如 "1.0.0"
//   platform         平台标识，如 "win-x64" / "mac-arm64"
//   callback         检查完成后的回调，在主线程执行。
//
// 行为：
//   · 进程级去重：同一进程仅执行一次更新检查。
//   · 网络超时 5 秒，失败或超时静默返回 has_update=false。
void CheckForUpdatesAsync(
    const juce::String& product,
    const juce::String& current_version,
    const juce::String& platform,
    std::function<void(const UpdateInfo&)> callback);

// 展示更新对话框（必须在主线程调用）。
//   · 包含"Download"、"Remind Me Later"两个按钮
//   · "Download" → 在默认浏览器打开 download_url
//   · force_update 时只显示 "Download"，不可关闭
void ShowUpdateDialog(const UpdateInfo& info);

}  // namespace network
}  // namespace organic

// ==========================================================
// network/UpdateChecker.cpp
//   软件更新检查实现。
//
//   工作流程：
//     1. 构造 GET URL → 投递到后台线程执行 HTTP 请求
//     2. 解析服务端 JSON 响应 → 提取 UpdateInfo
//     3. 通过 MessageManager::callAsync 切回主线程执行回调
//     4. 回调中若 has_update==true，弹 UpdateDialog 提示用户
// ==========================================================

#include "UpdateChecker.h"
#include "Version.h"
#include "../ui/UpdateDialog.h"

#include <JuceHeader.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace organic {
namespace network {

namespace {

// 服务端更新接口基础地址
constexpr const char* kUpdateCheckEndpoint = "https://iisaacbeats.cn/api/update/check";

// 从服务端 JSON 解析 UpdateInfo
UpdateInfo ParseUpdateResponse(const juce::var& json) {
  UpdateInfo info;
  if (auto* obj = json.getDynamicObject()) {
    info.has_update = obj->getProperty("has_update");
    info.latest_version = obj->getProperty("latest_version").toString();
    info.download_url = obj->getProperty("download_url").toString();
    info.changelog = obj->getProperty("changelog").toString();
    info.force_update = obj->getProperty("force_update");
  }
  return info;
}

// 后台线程登记器：持有所有进行中的后台线程，进程退出（static 析构阶段）
// 时统一 join，避免线程游离被 OS 强制终止。join 最多等待网络超时（5 秒）。
struct BackgroundThreadRegistry {
  ~BackgroundThreadRegistry() {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& t : threads) {
      if (t.joinable()) {
        t.join();
      }
    }
  }

  std::mutex mutex;
  std::vector<std::thread> threads;
};

BackgroundThreadRegistry& GetBackgroundThreadRegistry() {
  static BackgroundThreadRegistry registry;
  return registry;
}

void LaunchBackgroundThread(std::function<void()> fn) {
  auto& registry = GetBackgroundThreadRegistry();
  std::lock_guard<std::mutex> lock(registry.mutex);
  registry.threads.emplace_back(std::move(fn));
}

}  // namespace

// --------------------------------------------------
// ShowUpdateDialog（公开，供启动入口调用）
// --------------------------------------------------
void ShowUpdateDialog(const UpdateInfo& info) {
  // 确保在主线程
  jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

  // 优先使用自定义风格弹窗
  auto* dlg = organic::ui::UpdateDialog::ShowInComponent(info);
  if (dlg != nullptr) {
    return;
  }

  // 回退路径：无可用 UI 组件时使用 NativeMessageBox
  juce::String message;
  message << "A new version of Organic Chemistry is available!\n\n"
          << "Latest:  " << info.latest_version << "\n\n";

  if (info.changelog.isNotEmpty()) {
    message << "What's new:\n" << info.changelog << "\n\n";
  }

  message << "Would you like to download it?";

  juce::NativeMessageBox::showAsync(
      juce::MessageBoxOptions()
          .withIconType(info.force_update
                            ? juce::MessageBoxIconType::WarningIcon
                            : juce::MessageBoxIconType::QuestionIcon)
          .withTitle("Update Available")
          .withMessage(message)
          .withButton("Download")
          .withButton("Remind Me Later"),
      [info](int result) {
        // 0 = 第一个按钮 (Download)，也可能表示关闭对话框
        if (result == 0) {
          if (info.download_url.isNotEmpty()) {
            juce::URL(info.download_url).launchInDefaultBrowser();
          } else {
            juce::URL("https://iisaacbeats.cn").launchInDefaultBrowser();
          }
        }
      });
}

// --------------------------------------------------
// CheckForUpdatesAsync
//
//   进程级去重：static atomic 确保同一进程内仅执行一次更新检查，
//   避免 Standalone 模式下 Processor 构造与 App 初始化重复触发。
// --------------------------------------------------
void CheckForUpdatesAsync(
    const juce::String& product,
    const juce::String& current_version,
    const juce::String& platform,
    std::function<void(const UpdateInfo&)> callback) {
  static std::atomic<bool> s_checked_this_session{false};
  if (s_checked_this_session.exchange(true, std::memory_order_acquire)) {
    // 已经由其他入口触发过更新检查，直接返回空结果
    if (callback) {
      juce::MessageManager::callAsync(
          [callback]() { callback(UpdateInfo{}); });
    }
    return;
  }

  juce::String urlStr = kUpdateCheckEndpoint;
  urlStr << "?product=" << juce::URL::addEscapeChars(product, false)
         << "&version=" << juce::URL::addEscapeChars(current_version, false)
         << "&platform=" << juce::URL::addEscapeChars(platform, false);
  const auto url = juce::URL(urlStr);

  // 后台线程执行 HTTP GET（句柄由登记器持有，进程退出时统一 join）
  LaunchBackgroundThread([url, callback, current_version]() {
    UpdateInfo info;

    auto opts = juce::URL::InputStreamOptions(
                    juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs(5000)
                    .withExtraHeaders("Accept: application/json\r\n")
                    .withHttpRequestCmd("GET");

    if (auto stream = url.createInputStream(opts)) {
      const auto body = stream->readEntireStreamAsString();
      if (body.isNotEmpty()) {
        const auto json = juce::JSON::parse(body);
        info = ParseUpdateResponse(json);
      }
    }

    // 兜底：本地再比较一次，避免异常数据导致误弹
    if (info.has_update && info.latest_version.isNotEmpty()) {
      const int cmp = CompareVersionStrings(current_version, info.latest_version);
      if (cmp >= 0) {
        info.has_update = false;
      }
    }

    if (callback) {
      juce::MessageManager::callAsync(
          [callback, info]() { callback(info); });
    }
  });
}

}  // namespace network
}  // namespace organic

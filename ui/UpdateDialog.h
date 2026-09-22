// ==========================================================
// ui/UpdateDialog.h
//   自定义 Organic Chemistry 风格更新提示弹窗（白底医疗风）。
//
//   通过 addToDesktop 创建独立原生小窗口（480×340），setAlwaysOnTop
//   确保置顶。标题栏区域支持鼠标拖拽移动窗口位置。
//   两个操作按钮：Download / Remind Me Later。
//   force_update 时只显示 Download，不可关闭。
//   按钮操作后自删除（removeFromDesktop + MessageManager::callAsync）。
// ==========================================================

#pragma once

#include <JuceHeader.h>

#include <functional>

#include "../network/UpdateChecker.h"

namespace organic {
namespace ui {

class UpdateDialog : public juce::Component {
public:
  UpdateDialog(const organic::network::UpdateInfo& info,
               std::function<void()> onClose = {});

  void paint(juce::Graphics& g) override;
  void mouseDown(const juce::MouseEvent& e) override;
  void mouseDrag(const juce::MouseEvent& e) override;
  void mouseMove(const juce::MouseEvent& e) override;
  void mouseUp(const juce::MouseEvent& e) override;

  // 便捷工厂：创建独立原生小窗口并居中于主显示器。
  static UpdateDialog* ShowInComponent(const organic::network::UpdateInfo& info,
                                       std::function<void()> onClose = {});

private:
  enum class ButtonId { kDownload = 0, kRemind };

  // 对话框面板尺寸
  static constexpr int kDlgW = 480;
  static constexpr int kDlgH = 340;

  // 按钮尺寸
  static constexpr int kBtnW = 140;
  static constexpr int kBtnH = 28;
  static constexpr int kBtnGap = 12;

  // 标题栏高度
  static constexpr int kTitleBarH = 28;

  organic::network::UpdateInfo info_;
  std::function<void()> onClose_;

  int hoveredBtn_ = -1;
  int pressedBtn_ = -1;

  // 标题栏拖拽
  bool isDragging_ = false;
  juce::Point<int> dragOffset_;

  juce::Rectangle<int> GetTitleBarBounds() const;
  juce::Rectangle<int> GetButtonBounds(int idx) const;
  int HitTestButton(juce::Point<int> pos) const;
  bool HasRemindButton() const;
  void ExecuteButton(int idx);
  void CloseDialog();

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UpdateDialog)
};

}  // namespace ui
}  // namespace organic

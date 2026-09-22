#pragma once

#include <JuceHeader.h>
#include "MoleculeModel.h"

// ============================================================
//  PresetMenu — 自绘的分子预设选择面板
//
//  为什么不用 juce::PopupMenu：
//    PopupMenu 走的是宿主系统的 LookAndFeel，弹出来是一块灰蓝色的系统风格
//    菜单，和这个插件"白底 / 细描边 / 无渐变"的语言完全不搭，而且在不同
//    DAW 里长相还不一样。这里直接画一块卡片，风格由自己完全掌控。
//
//  结构：
//    组件本身铺满整个编辑器，充当**遮罩层**（点卡片外面任意位置即关闭），
//    卡片绘制在顶栏正下方居中。列数固定为 3，行数由条目数决定，因此 33 条
//    预设一屏放得下，不需要滚动。
//
//  每行左边是英文名、右边是灰色的分子式，选中项有一条左侧强调竖条。
// ============================================================

namespace organic
{

class PresetMenu : public juce::Component
{
public:
    PresetMenu();
    ~PresetMenu() override = default;

    void paint (juce::Graphics& g) override;

    void mouseMove (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;

    /** 全局界面缩放（与编辑器一致）。 */
    void setUiScale (float s) { uiScale = s; }

    /** 当前已加载的预设序号（−1 表示用户手改过，不对应任何预设）。 */
    void setCurrentIndex (int index) { currentIndex = index; }

    /** 卡片顶边在父组件坐标系中的 y 值（一般是顶栏底部 + 一点间距）。 */
    void setAnchorY (int y) { anchorY = y; }

    std::function<void (int)> onChosen;    // 选中某个预设
    std::function<void()>     onDismiss;   // 点空白处关闭

private:
    /** 卡片矩形（父组件坐标系）。 */
    juce::Rectangle<int> cardBounds() const;

    /** 第 index 项的行矩形。 */
    juce::Rectangle<int> itemBounds (int index) const;

    /** 命中测试：返回条目序号，未命中返回 −1。 */
    int itemAt (juce::Point<int> p) const;

    int   rowCount() const;

    float uiScale     = 1.0f;
    int   anchorY     = 48;
    int   currentIndex = -1;
    int   hoveredIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetMenu)
};

} // namespace organic

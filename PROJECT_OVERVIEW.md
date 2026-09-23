
# Organic Chemistry 项目全景简介（AI 上下文导航文档）

> 本文档是为 AI 助手上下文初始化设计的项目导航说明。阅读完本文档，你应能立刻定位到"改哪个文件、调哪个类、走哪条数据流"。
>
> 本项目特殊性：**它把有机化学的成键规则编码进了 UI 交互**。因此本文档专门设立 §7「化学知识来源与代码对照」，把每一条化学规则的出处、简化程度、以及它落在哪个函数里逐条列清，便于后续校正或替换为专业库。

---

## 1. 项目概述

### 1.1 项目定位
- **产品名**：`Organic Chemistry`（版本 `1.0.1`）
- **产品形态**：一款以 **分子结构编辑器作为主交互界面** 的 **Bell 音色合成器**（Synth / 乐器插件）。核心是一个复刻 Vital「BELL Reflections」预设的三正弦钟声引擎；用户在界面中"搭建分子"，分子的规范 SMILES 经确定性哈希映射到 Bell 引擎的 33 个参数——**分子即预设**。
- **产品分类**：`IS_SYNTH TRUE` + `NEEDS_MIDI_INPUT TRUE`，AU 注册为 `kAudioUnitType_MusicDevice`（乐器）。
- **发行形态**（[CMakeLists.txt](/I:/Organic%20Chemistry/CMakeLists.txt) 中 `juce_add_plugin`）：
  - **Windows**：`VST3` + `Standalone`
  - **macOS**：`VST3` + `Standalone` + `AU`
  - **BundleID**：`cn.iisaacbeats.OrganicChemistry`，PluginCode `OrCh`
- **视觉方向**：**亮白极简医疗风**（clinical minimal）。白底、细描边、低饱和 CPK 元素配色、克制的呼吸动画。与作者其它插件（Y2KMeter 的 Pink XP 像素风、Pupon / CRTLoss 的地下失真风）刻意区分。

### 1.2 当前阶段与主要功能

**已完成（v1.0.1）**：
- 底部元素栏选择 C / O / N / S / P
- 画布**按下拖拽放置原子**：按下显示虚影（ghost），松手才落子
- 从一个原子**拖到另一个原子**成键连接（受价键约束，无法连接的不支持）
- 右键删除原子，**断链后只保留最大片段**（画布恒为单一分子）
- 右键**断裂化学键**（可打开环 / 拆分子，同样保留最大片段）
- 点击化学键循环切换 **单键 / 双键 / 三键**（受两端剩余价键约束）
- 分子随原子增多**自动缩小视图**（auto-fit）
- 顶栏显示 **分子式（Hill 式）+ 分子量**
- 画布左下角显示结构式：无环用**凝聚式**（如 `CH3-CH2-OH`），含环用 **SMILES 环结构式**（如 `C1=CC=CC=C1`）
- 识别**常见物质**并在左下角显示英文**常用名**（如 Benzene / Water / Carbon dioxide）
- 窗口右下角拖动**等比缩放**整个界面（50 % ~ 200 %）
- **顶栏分子预设**：点击分子式展开**自绘**的预设面板（3 列 × 33 种常见有机分子，含分子式），
  左右 `<` `>` 循环切换（见 §7.9）
- **常用名识别 78 种**：预设 33 + 扩展 45，名字表由拓扑在运行时反算规范 SMILES 自动建立（见 §7.10）
- **画布为空时静音**：没有分子就没有音色，输出被 12 ms 平滑门压到零并掐掉发声中的音符
- **VSEPR 键角力场**：键长弹簧 + 角弯曲 + 1-2/1-3 排除的非键斥力，烷烃链呈现正确的 112° 锯齿而非直线（见 §7.5）
- **sp³ 键角按元素取实验值**：C/N/O/S/P 各不相同（C–S–C 只有 99°，画出来明显比碳链尖），
  不再是一刀切的 109.5°（见 §7.5.3b）
- **环几何**：环内键角取多边形内角 + 环模板力，环己烷 / 苯稳定为正六边形，稠环（萘）按共享边镜像展开（见 §7.5.6b）
- **Bell 音色引擎**：三正弦振荡器复刻 Vital「BELL Reflections」预设（OSC1 基频 / OSC2 +12 八度 + squeeze 相位失真 / OSC3 +12 八度），
  → 低通滤波（keytrack + ENV2）→ 多段压缩 → 合唱 → 延迟 → 混响 → 降采样 → 软削波（见 §9）
- **钟形包络**：快起音 + 指数衰减 + 长释放，配 ENV2 调制包络（推动滤波截止）
- **分子 → Bell 参数映射**：分子 SMILES 经确定性哈希映射到 **33** 个 Bell 参数 + 4 个宏（WET / BITCRUSH / DETUNE / ATTACK），
  非单调、确定但不可预测（**完整对照见 §1.5**）
- **EnvelopePanel（未接线）**：ADSR 图形编辑器组件文件已写好，但尚未接入界面；当前 ADSR 完全跟随分子
- **波形预览**：右上角实时显示 osc_1 的分子波表（近正弦 + 少量 SMILES 哈希决定的谐波）
- **Test 面板（v1.0.0 起屏蔽）**：33 个 Bell 参数按分类展示，`kTestPanelEnabled = false` 暂时隐藏
- **自动更新检查 + 更新弹窗**：启动后延迟 5s 异步请求 `iisaacbeats.cn/api/update/check`（5s 超时、失败静默），仅有新版本时弹原生更新窗（`network/` + `ui/UpdateDialog`）
- **每日匿名遥测**：`shared/IisaacTelemetry.h`，每日一次 `ui_open` 事件（无音频数据、无 PII），`IISAAC_TELEMETRY_DISABLED=1` 可关闭

**尚未实现（明确推迟）**：
- 参数自动化（`AudioProcessorValueTreeState`）、Bell 音色预设管理（分子预设已有，音色预设尚无）。

### 1.3 技术栈

| 项目 | 版本 / 说明 |
| --- | --- |
| 语言 | C++17（`CMAKE_CXX_STANDARD 17`，`CXX_EXTENSIONS OFF`） |
| 框架 | **JUCE 8.0.12**（通过 `FetchContent` 拉取，`GIT_TAG 8.0.12`） |
| 构建 | CMake ≥ 3.22；本机实际用 **CMake 4.3.3 + Ninja + VS18 Professional** |
| 化学计算 | **全部自研**（无外部化学库依赖）：VSEPR 键角力场、SSSR 环感知、Hückel 芳香性、21 个分子描述符。规则与文献出处见 §7 |
| 渲染 | 纯 `juce::Graphics`（CPU 软件渲染，无 OpenGL） |
| 动画 | 单一 `juce::Timer` @ 60 Hz，见 §4.1 |
| MSVC 选项 | `/utf-8`（源码含中文注释） |
| 依赖库 | 仅 `juce::juce_audio_utils`（Standalone 需要） |

### 1.4 为什么不依赖 RDKit / Avogadro2

开发过程中评估并**实际编译验证**了两个候选库（见 §7.5），最终选择自研简化模型，理由：

| 候选 | 结论 | 原因 |
| --- | --- | --- |
| **RDKit** | 不采用 | C++ 核心 + Python 绑定，但 Windows 下 C++ 静态链接体积庞大（含 Boost 依赖），且插件只需"成键规则 + 价键计数"这一小部分能力 |
| **Avogadro2**（avogadrolibs） | 已编译验证可用，暂不集成 | 最小配置（`USE_QT=OFF`）能编出纯 C++ 的 `Core.lib` + `IO.lib`（见 §7.5），SMILES 解析和分子数据结构可用。但当前交互只需二维布局与价键约束，引入它会带来 Eigen 依赖与额外体积。**保留为后续升级路径** |
| **自研** | ✅ 当前方案 | 全部逻辑约 700 行，零外部依赖，可完全控制动画与交互耦合 |

> **升级路径**：若将来需要真实几何优化（MMFF94 力场）、SMILES 导入导出、芳香性判定，可将 `MoleculeModel` 的几何部分替换为 Avogadro2 的 `avogadro::calc` 模块。已验证的构建方式记录在用户级 skill `avogadro2-windows-build` 中。

---

### 1.5 分子 → Bell 音色映射（调试速查）⭐⭐

> **这一节是调音的唯一入口。** 想改某个分子对应的听感，先去 `BellEngine.cpp::mapMoleculeToBellParams()`
> 改那一行。全部 33 个 Bell 参数 + 4 个宏在此列全。DSP 实现见 §9，化学描述符见 §7.7。

#### 1.5.1 数据流

```
Molecule（画布拓扑）
   │
   ├─ canonicalSmiles()  ──────────────► buildNearSineWave()  ──► osc_1 波表（近正弦）
   │                                                              （SMILES 哈希决定谐波）
   ├─ canonicalSmiles()  ──────────────► mapMoleculeToBellParams() ──► 33 个 Bell 参数
   │                                                                  （本节表格）
   └─ canonicalSmiles()  ──► hashSmiles() ──► setNoiseSampleIndex() ──► 击打采样
```

**核心设计**：分子不再是"化学性质 → 合成参数"的单调映射，而是把规范 SMILES 喂给一个
**确定性哈希 + xorshift32 伪随机源**，得到一串稳定但不可预测的值，落在「冬季钟声/键盘」
好音色的共性范围内：

- **确定**：同一分子永远得到同一音色（可复现）。
- **不可预测**：加/删一个原子会让音色**无规律地**改变，而不是单调地"更亮/更响"——
  这是刻意为之，让分子像"随机种子"而非"旋钮"。

#### 1.5.2 手动锁定

当前**没有**手动锁定入口：全部 33 个 Bell 参数（含 ADSR 的 Attack/Decay/Release，Sustain 固定 0）
完全跟随分子 SMILES 哈希。`EnvelopePanel` 组件文件已写好但尚未接线/编译（见 §2）。

#### 1.5.3 声源（三个正弦振荡器）

| 参数 | 映射 | 说明 |
| --- | --- | --- |
| OSC1 level | `0.65 + hash·0.2` | 基频层音量（0.65–0.85） |
| OSC1 pitch | 40% 概率 +12 | 基频八度（0 或 +12） |
| OSC2 pitch | 40% 概率 +24 | 高八度铃音层（+12 或 +24） |
| OSC2 squeeze | `hash` | 相位失真量 0–1（金属感） |
| OSC2 unison | `1 + floor(hash·7)` | 齐奏声部数 1–7（厚度） |
| OSC3 level | `hash·0.35` | 最亮高层音量（0–0.35） |
| OSC3 pitch | 50% 概率 +24 | 高层八度（+12 或 +24） |

#### 1.5.4 包络（钟形）与滤波

| 参数 | 映射 | 说明 |
| --- | --- | --- |
| Attack | `0.0005 + hash·0.015` | 0.5–15 ms 快起音（敲击感） |
| Decay | `0.8 + hash·2.5` | 0.8–3.3 s 指数衰减到 0 |
| Sustain | 固定 0 | 钟声无平台 |
| Release | `0.6 + hash·2.0` | 0.6–2.6 s 长余音 |
| ENV2 → filter | `hash` | 音头把滤波截止推开 0–1 |
| Filter cutoff | `400 + hash·4000` | 400–4400 Hz 低通（osc_2 路由经过） |
| Filter resonance | `hash·0.3` | Q 0.707–约 2.9 |
| Filter keytrack | 固定 1.0 | 截止频率跟随音高（E9 溢出已修复，见 §12） |

#### 1.5.5 噪声击打层

| 参数 | 映射 | 说明 |
| --- | --- | --- |
| Noise level | 70% 概率 `0.3 + hash·0.5`，否则 0 | 金属击打瞬态（noise 采样库） |
| Noise cutoff | `1000 + hash·1000` | 击打采样带通中心 1000–2000 Hz |

#### 1.5.6 空间效果与失真

| 参数 | 映射 | 说明 |
| --- | --- | --- |
| Chorus mix | `0.05 + hash·0.25` | 4 声部调制延迟干湿比 |
| Chorus feedback | `0.35 + hash·0.15` | 0.35–0.50 |
| Delay mix | `0.15 + hash·0.3` | 乒乓延迟干湿比 |
| Delay feedback | `0.2 + hash·0.3` | 0.2–0.5 |
| Delay time | `0.3 + hash·0.5` | 0.3–0.8 s 回声间隔 |
| Reverb mix | `0.2 + hash·0.4` | 空间湿声 |
| Reverb decay | `0.4 + hash·1.0` | 混响衰减 |
| Reverb size | `0.3 + hash·0.3` | 房间大小 |
| Distortion drive | `hash·0.5` | 降采样失真深度 |
| Distortion mix | `hash·0.3` | 失真干湿比 |

#### 1.5.7 四个宏

| 宏 | 默认 | 作用 |
| --- | --- | --- |
| WET | 0.5 | 效果链湿声总量（bipolar，正负都影响合唱/延迟/混响） |
| BITCRUSH | 0.0 | 降采样失真深度 |
| DETUNE | 0.2 | 慢速音高漂移深度（±22.5 音分） |
| ATTACK | 哈希 | osc_2 电平（铃音层起音强度） |

#### 1.5.8 想听某个变化，该改什么分子

分子编辑**不再有**「加双键 → 更亮」这类单调直觉：改任意结构都会让整个音色**无规律地换一个**
（SMILES 哈希重排所有参数）。唯一确定的行为：

| 操作 | 结果 |
| --- | --- |
| 画布留空 | 静音（`setMoleculeEmpty(true)`） |
| 放任意重原子 | 立即出声，音色由该分子的 SMILES 哈希决定 |
| 改动任意原子/键 | 整个音色换一个（不可预测，但同分子可复现） |
| 手动调节包络 | 当前无入口（EnvelopePanel 未接线，Test 面板已屏蔽） |

---

## 2. 文件结构与分层

```
I:\Organic Chemistry\
├── CMakeLists.txt              JUCE 插件定义 + 源文件列表
├── README.md                   项目 README（双语）
├── LICENSE                     GNU GPL v3.0
├── PROJECT_OVERVIEW.md         本文档
├── PluginProcessor.h/.cpp      音频处理器：Bell 效果链 + 采样库 + 状态持久化
├── PluginEditor.h/.cpp         顶层编辑器：布局 / 信息栏 / 缩放 / 动画时钟
├── BellEngine.h/.cpp           【Bell 引擎】三正弦振荡器 voice + 分子→Bell 参数映射
├── BellWave.h                  osc_1 默认波表常量
├── EnvelopePanel.h/.cpp        ADSR 图形编辑器（组件已写好，未接线/未编译）
├── MoleculeModel.h/.cpp        【化学核心】分子数据结构 + 成键规则 + 布局求解
├── MoleculeCanvas.h/.cpp       画布：坐标变换 / 命中测试 / 绘制 / 交互
├── ElementBar.h/.cpp           底部元素选择栏
├── MoleculeAudioMapper.h/.cpp  （历史）类型定义 + kWaveTableSize + kOsc1Wave
├── TestPanel.h/.cpp            33 个 Bell 参数调试面板（表驱动，v1.0.0 起屏蔽）
├── PresetMenu.h/.cpp           自绘分子预设面板（3 列卡片）
├── AudioTests.cpp / BellTests.cpp   离线回归测试
├── network/                    SemVer 版本解析 + 异步更新检查
├── ui/                         更新弹窗（白底医疗风）
├── shared/IisaacTelemetry.h    header-only 每日匿名遥测
├── noise/                      噪声击打采样库（120 个 wav/aif）
├── build_installer.bat         Windows 安装器打包（Inno Setup）
├── build_installer_mac.sh      macOS 安装器打包（Universal pkg/dmg）
├── organic_chemistry_installer.iss   Inno Setup 安装脚本
├── cmake-build-ninja/          Ninja 构建目录
└── cmake-build-release-visual-studio/   VS 构建目录
```

### 2.1 分层架构

```
┌──────────────────────────────────────────────────────────────┐
│  Plugin 层                                                     │
│    OrganicChemistryAudioProcessor  (PluginProcessor.h/cpp)    │
│      · processBlock → processBellBlock（Bell 模式）            │
│      · 效果链：多段压缩 → 合唱 → 延迟 → 混响 → 降采样 → 软削波   │
│      · 噪声击打采样库 + 分子状态持久化                          │
│    OrganicChemistryAudioProcessorEditor (PluginEditor.h/cpp)  │
│      · 唯一的 Timer（60Hz）→ 驱动所有子组件动画                  │
│      · ComponentBoundsConstrainer 锁定宽高比 → 等比缩放          │
│      · applyMoleculeToAudio：分子 → Bell 参数 + osc_1 波表      │
├──────────────────────────────────────────────────────────────┤
│  音频引擎层（BellVoice，BellEngine.h/cpp）                      │
│    · 三正弦振荡器（OSC1/2/3）+ squeeze 相位失真                 │
│    · 钟形包络（ADSR + ENV2）+ 低通滤波（keytrack + ENV2）       │
│    · 噪声击打采样（bandpass）                                  │
├──────────────────────────────────────────────────────────────┤
│  UI 组件层                                                     │
│    MoleculeCanvas  —— 分子画布（模型空间 ↔ 屏幕空间变换）        │
│    ElementBar      —— C/O/N/S/P 圆形色标                       │
│    EnvelopePanel   —— ADSR 图形编辑器（手动锁定段）             │
│    PresetMenu      —— 自绘预设面板（全屏遮罩 + 3 列卡片）         │
│    TestPanel       —— 33 个 Bell 参数调试面板（已屏蔽）          │
├──────────────────────────────────────────────────────────────┤
│  映射层（纯函数，无状态）                                        │
│    BellEngine.cpp  —— mapMoleculeToBellParams（SMILES→33 参数） │
│                    —— buildNearSineWave（SMILES→osc_1 波表）    │
├──────────────────────────────────────────────────────────────┤
│  Model 层（无 JUCE UI 依赖，纯数据 + 算法）                      │
│    organic::Molecule  —— 拓扑、价键、补氢、VSEPR 力场、描述符     │
│    organic::Element / ElementInfo —— 元素属性表                 │
└──────────────────────────────────────────────────────────────┘
```

**依赖方向**：`PluginEditor` → `MoleculeCanvas` → `Molecule`。`Molecule` 不反向依赖任何 UI 类型（除 `juce::Point` / `juce::String` 这类值类型），便于将来单独做单元测试或替换实现。

### 2.2 关键调用关系

1. **用户点击 → 拓扑变化**：
   `MoleculeCanvas::mouseDown` → `screenToModel()` 转坐标 → `Molecule::addAtom / removeAtom / cycleBondOrder` → `rebuildHydrogens()` → 回调 `onMoleculeChanged` → `PluginEditor` 重绘顶栏
2. **动画帧**：
   `PluginEditor::timerCallback`（60 Hz）→ `canvas.advanceAnimation(dt)` + `elementBar.advanceAnimation(dt)`
   → 画布内部：`Molecule::relaxStep()`（力导向一步）+ `Molecule::advanceAnimations()`（出生/键级动画）+ `updateViewScale()`（auto-fit）
3. **窗口缩放**：
   拖动 `ResizableCornerComponent` → `constrainer` 按宽高比修正 → `resized()` → `uiScale()` 重算 → `canvas.setUiScale()` / `elementBar.setUiScale()` → 所有字号、线宽、半径乘以该因子

---

## 3. 坐标系统（重要）

这是理解画布代码的关键。共有两个坐标空间：

| 空间 | 定义 | 谁在用 |
| --- | --- | --- |
| **模型空间（model space）** | 分子自身的坐标，无边界。键长恒为 62 px（重原子间）/ 34 px（C–H），与窗口大小无关 | `Molecule` 内部全部计算（力导向、命中测试、包围盒） |
| **屏幕空间（screen space）** | 画布组件的局部像素坐标 | `juce::Graphics` 绘制、鼠标事件 |

**变换公式**（[MoleculeCanvas.cpp](/I:/Organic%20Chemistry/MoleculeCanvas.cpp)）：

```cpp
drawScale() = uiScale * viewScale        // 总缩放
modelToScreen(p) = canvasCentre() + (p - modelCentre) * drawScale()
screenToModel(p) = modelCentre + (p - canvasCentre()) / drawScale()
```

- `uiScale`：窗口缩放因子（问题 2），由 `PluginEditor` 下发
- `viewScale`：**自动适配因子**（问题 3），由 `updateViewScale()` 按分子包围盒计算，范围 `[0.22, 1.0]`
- `modelCentre`：分子包围盒中心，用于保持分子视觉居中

**为什么要分离**：力导向求解必须在稳定的模型空间进行，否则窗口缩放会改变物理量纲导致布局抖动。所有鼠标事件在进入 `Molecule` 前必须先 `screenToModel()`。

---

## 4. 动画系统

### 4.1 单一时钟原则

**所有动画由 `PluginEditor` 的一个 `juce::Timer`（60 Hz）驱动**，子组件不持有 Timer。

```cpp
void PluginEditor::timerCallback() {
    constexpr float dt = 1.0f;              // dt 单位为「帧」
    canvas.advanceAnimation (dt);
    elementBar.advanceAnimation (dt);
}
```

**理由**：早期版本若让每个组件各自 `startTimerHz`，多个 Timer 会互相抢时钟，造成 repaint 抖动与不必要的开销。子组件自行判断是否需要 `repaint()`。

### 4.2 动画清单

| 动画 | 位置 | 速率 | 说明 |
| --- | --- | --- | --- |
| 原子出生 | `Atom::spawnAnim` | `dt * 0.09` | 从 55 % 放大淡入，约 11 帧完成 |
| 键级切换 | `Bond::orderAnim` | `dt * 0.12` | 双键/三键的平行线间距从 0 张开 |
| 悬停高亮 | `MoleculeCanvas::hoverAnim` | `dt * 0.18` | 原子光环 / 键变蓝加粗 |
| 点击涟漪 | `MoleculeCanvas::rippleAnim` | `dt * 0.05` | 一次性扩散圆环，约 20 帧 |
| 视图 auto-fit | `MoleculeCanvas::viewScale` | `dt * 0.07` 缓动 | 向 `viewScaleTarget` 平滑逼近，避免突跳 |
| 元素栏选中 | `ElementBar::Slot::selectAnim` | `dt * 0.16` | 圆点放大 + 呼吸光环 |
| 元素栏呼吸环 | `ElementBar::breathPhase` | `dt * 0.055` | 选中项的光环脉动 |

---

## 5. 六个交互问题的实现对照

本节对应用户提出的六项修改需求，便于回溯。

### 问题 1：界面全英文化
- **改动**：`ElementInfo::name` 由中文改为 `"Carbon" / "Oxygen" / ...`；空画布提示、`Clear` 按钮、`STRUCTURE` 标签全部英文
- **原因**：中文在部分环境下渲染为乱码（JUCE 默认字体回退问题）；英文同时更契合医疗风的克制感

### 问题 2：等比缩放
- **位置**：[PluginEditor.cpp](/I:/Organic%20Chemistry/PluginEditor.cpp) 构造函数
- **实现**：
  ```cpp
  constrainer.setFixedAspectRatio (820.0 / 560.0);
  constrainer.setSizeLimits (410, 280, 1640, 1120);   // 50% ~ 200%
  setConstrainer (&constrainer);
  setResizable (true, false);
  resizer = std::make_unique<juce::ResizableCornerComponent> (this, &constrainer);
  ```
- **缩放传导**：`uiScale() = getWidth() / kDesignWidth`，在 `resized()` 中下发给两个子组件。所有字号、线宽、半径、边距均乘以该因子

### 问题 3：分子变大时自动缩小（无边界自由舒展）
- **位置**：`MoleculeCanvas::updateViewScale()`
- **前置改动**：已移除圆形培养皿边界（`paintDish` / `dishPhase` 全部删除），分子不再被限制在固定半径内
- **算法**：
  1. 取 `Molecule::boundingBox()`（含原子半径）
  2. 可用空间 = 画布矩形宽高各减 24 px 边距（`kFitPadding`）
  3. `viewScaleTarget = clamp(min(usableW / extentW, usableH / extentH), 0.22, 1.0)`
     - 上限 1.0：小分子保持自然大小，不放大
     - 下限 0.22：极大分子仍可辨认
  4. `viewScale` 以 `dt * 0.07` 缓动逼近目标
- **副作用处理**：原子标签在 `r <= 6.0f` 时不绘制，避免小字糊成一团

### 问题 4：删除后只保留最大分子
- **位置**：`Molecule::keepLargestFragment()`，由 `removeAtom()` 调用
- **算法**：BFS 连通分量标记 → 统计各分量原子数 → 保留最大者 → 索引重映射压缩数组
- **确定性**：`std::max_element` 遇到并列时返回首个，因此结果稳定可复现
- **调用顺序**：`removeAtom` → 剔除目标原子 → `keepLargestFragment()` → `rebuildHydrogens()`

### 问题 5：点击键切换键级
- **命中测试**：`Molecule::hitTestBond()` 用**点到线段距离**，并排除落在两端原子圆内的点击（那属于原子）
- **切换规则**：`cycleBondOrder()` → `单键 → 双键 → 三键 → 单键`
- **约束**：`maxOrderForBond()` 计算两端剩余价键的**较小值**：
  ```cpp
  freeA = valence(A) - (heavyBondOrderSum(A) - currentOrder)
  freeB = valence(B) - (heavyBondOrderSum(B) - currentOrder)
  maxOrder = clamp(min(freeA, freeB), 1, 3)
  ```
  例：`CH3-CH3` 两端各剩 3 个氢 → 可升到三键；`CH3-OH` 的 O 只有 2 价 → 最高双键
- **绘制**：单键 1 条线；双键 2 条平行线（法向偏移 `3.2 * scale`）；三键中间 1 条 + 两侧各 1 条
- **氢键不可点**：`hitTestBond` 跳过含氢的键（H 恒为单键）
- **联动**：升键级会消耗氢，因此切换后调用 `rebuildHydrogens()`

### 问题 6：左下角结构式
- **位置**：`MoleculeCanvas::paintStructuralFormula()`，数据来自 `Molecule::structuralFormula()`
- **算法**：见 §7.4

### 问题 7：拖拽放置原子 + 拖拽成键
- **位置**：`MoleculeCanvas::mouseDown / mouseDrag / mouseUp` + `paintGhost()`
- **交互**：左键按下即进入拖拽态（`dragActive = true`），显示半透明虚影跟随指针；松手才真正落子
  - 空白处按下 → 拖到空白松手：放置当前元素
  - 原子 A 上按下 → 拖到空白松手：从 A 生长新原子（沿 A → 指针方向）
  - 原子 A 上按下 → 拖到原子 B 松手：在 A、B 间新建单键
- **虚影**：`paintGhost()` 绘制半透明原子 + 连接线 + 目标原子高亮；无效位置（不可放置/成键）时置灰
- **化学约束**：`Molecule::canAddAtom() / canGrowFrom() / canConnect()` 实时判断有效性，无效时松手不生效

### 问题 8：右键断裂化学键
- **位置**：`MoleculeCanvas::mouseDown` 右键分支 + `Molecule::removeBond()`
- **交互**：右键命中原子 → 删原子；命中键（不在原子上）→ 断键
- **断键语义**：`removeBond()` 拒绝氢键；删除键 → 剥离氢 → `keepLargestFragment()` → `rebuildHydrogens()`
- **用途**：打开环（断一根环键，分子仍连通不剪枝）或拆分子（断桥键，保留最大片段）

### 问题 9：环状结构式
- **位置**：`Molecule::hasRing()` + `ringStructuralFormula()`，由 `structuralFormula()` 分流
- **算法**：见 §7.4

### 问题 10：常见物质识别（常用名）
- **位置**：`Molecule::canonicalSmiles()` + `commonName()` + 内置物质表 `kCommonSubstances`
- **显示**：`paintStructuralFormula()` 在识别到常用名时于左下角上方加粗显示 `COMMON NAME`
- **数据**：内置 26 个常见物质的 `SMILES → 英文常用名` 静态表，见 §7.4.4

---

## 6. 关键类速查

### 6.1 `organic::Molecule`（MoleculeModel.h/.cpp）

| 方法 | 作用 |
| --- | --- |
| `addAtom(element, hint)` | 加重原子。空分子时落在 hint；否则挂到"有剩余价键且离 hint 最近"的重原子 |
| `connectAtoms(a, b)` | 在两个重原子间新建单键（须两端都有剩余价键且尚未成键） |
| `canAddAtom()` | 分子是否还能再加原子（空分子或存在有剩余价键的重原子） |
| `canGrowFrom(index)` | 指定重原子是否还有剩余价键可生长 |
| `canConnect(a, b)` | 两个重原子能否新建单键（供拖拽成键预判） |
| `removeAtom(index)` | 删重原子 → 保留最大片段 → 重建氢。氢原子拒绝删除 |
| `removeBond(bondIndex)` | 断键 → 保留最大片段 → 重建氢。氢键拒绝删除 |
| `cycleBondOrder(bondIndex)` | 循环键级，受 `maxOrderForBond` 约束 |
| `maxOrderForBond(bondIndex)` | 该键在价键约束下可达的最高键级 |
| `hitTestAtom(p, scale)` | 原子命中（模型空间） |
| `hitTestBond(p, tolerance)` | 键命中（点到线段距离，排除端点圆内） |
| `formula()` | Hill 式分子式 |
| `structuralFormula()` | 结构式（无环凝聚式 / 含环 SMILES） |
| `canonicalSmiles()` | 规范 SMILES（供常用名匹配） |
| `commonName()` | 匹配内置表的英文常用名，无匹配返回空 |
| `molecularWeight()` | 分子量 |
| `computeDescriptors()` | 计算分子级化学描述符（供音频映射层） |
| `boundingBox()` | 含原子半径的包围盒（供 auto-fit） |
| `stericNumber(i)` | VSEPR 立体数 = σ 键数 + 孤对数 |
| `idealBondAngle(i, heavyPair)` | 理想键角（弧度）。`heavyPair` 决定取甲基化还是氢化物实验值 |
| `perceiveRings()` | SSSR 近似，逐键 BFS。**不要在每帧循环里直接调** |
| `rings()` | `perceiveRings()` 的记忆化版本，按 `topologyVersion` 失效 |
| `relaxStep(dt)` | 力导向一步（质心整体回中，不改变分子形状），返回平均位移 |
| `advanceAnimations(dt)` | 推进出生 / 键级动画 |
| `toValueTree()` / `fromValueTree()` | 拓扑持久化（不含坐标，恢复时由 `seedLayout()` 重建） |

**私有关键方法**：
- `rebuildHydrogens()` —— 先清空所有氢与含氢键，再按 `valence - heavyBondOrderSum` 补齐。**所有拓扑变更的必经之路**，因此环缓存的失效点放在这里
- `ringAngleTarget(c, n1, n2)` —— 若 n1/n2 是 c 在某个环里的前驱与后继，返回该环的多边形内角，否则返回负数
- `seedLayout()` —— 环模板 + BFS 的确定性初始布局，供 `fromValueTree()` 使用
- `invalidateTopology()` —— 递增 `topologyVersion`，让 `rings()` 下次重算
- `heavyBondOrderSum(i)` —— 只统计与重原子相连的键级之和（**不含氢**）
- `keepLargestFragment()` —— BFS 连通分量剪枝
- `longestChain()` —— 双 BFS 求树直径（仅用于无环分支）
- `hasRing()` —— 环检测（重原子边数 ≥ 重原子数）
- `ringStructuralFormula()` —— SMILES 风格环结构式

### 6.2 `organic::MoleculeCanvas`

关键状态：`uiScale`（窗口缩放）、`viewScale` / `viewScaleTarget`（auto-fit）、`modelCentre`（分子中心）、`hoveredAtom` / `hoveredBond`。

绘制顺序（`paint`）：键 → 原子（先氢后重原子）→ 结构式 → 涟漪。

### 6.3 `organic::ElementBar`

`slots` 数组持有每个元素的 `bounds` + `selectAnim` + `hoverAnim`。选中通过 `onElementChosen` 回调通知外部。

### 6.4 `organic::BellEngine`（BellEngine.h/.cpp）

Bell 音色引擎：`BellVoice`（三正弦振荡器 + 包络 + 低通 + 噪声击打）、`BellPatch`（复刻 Vital BELL Reflections 的参数集）、`mapMoleculeToBellParams()`（SMILES 哈希 → 33 参数）、`buildNearSineWave()`（osc_1 近正弦波表）、`BellParamId`/`bellParamDef()` 参数元数据。详见 §9。

> `MoleculeAudioMapper.h/.cpp` 现在是历史遗留：只保留 `kWaveTableSize`、`WaveTable`、`kOsc1Wave` 等类型/常量定义，旧的 `mapMoleculeToAudio()`/`buildWaveTable()` 已不再被调用。

---

## 7. 化学知识来源与代码对照 ⭐

> **本节是本项目最需要外部校验的部分。** 所有化学规则均为教科书级基础知识的**简化实现**，用于交互原型，**不适用于严谨化学计算**。下表逐条列出规则、出处、简化程度与代码位置。

### 7.1 元素属性表

**代码位置**：[MoleculeModel.h](/I:/Organic%20Chemistry/MoleculeModel.h) `elementInfo()` 函数内的 `table[]`

| 元素 | 价键数 | 原子量 | 绘制颜色 | 半径(px) |
| --- | --- | --- | --- | --- |
| C 碳 | 4 | 12.011 | `#3A3A3A` 石墨黑 | 15.0 |
| O 氧 | 2 | 15.999 | `#D9544D` 砖红 | 13.5 |
| N 氮 | 3 | 14.007 | `#4A7BC4` 钢蓝 | 14.0 |
| S 硫 | 2 | 32.060 | `#D9A63A` 芥黄 | 16.5 |
| P 磷 | 3 | 30.974 | `#CE7A3C` 赭橙 | 16.0 |
| H 氢 | 1 | 1.008 | `#B8B8B8` 浅灰 | 7.5 |

**来源与依据**：

1. **价键数（valence）**
   - **来源**：中学 / 大学基础有机化学的「常见共价键数」，即中性、不带电荷状态下的典型成键数。
   - **依据**：碳四价、氧二价、氮三价、氢一价是有机化学最基础的成键规则（八隅体规则 / octet rule 的常见结果）。
   - **⚠️ 已知简化**：
     - **硫取 2 价**：硫在有机物中常见 2 价（硫醇 R–SH、硫醚 R–S–R），但也存在 4 价（亚砜）和 6 价（磺酸、硫酸根）。本项目**只取最简的 2 价**。
     - **磷取 3 价**：磷常见 3 价（膦 PH₃）与 5 价（磷酸酯、ATP 中的磷酸基）。本项目**只取 3 价**。
     - 不支持形式电荷（如 `NH4+` 的四价氮）、自由基、配位键。
   - **若需扩展**：应把 `valence` 从单个 int 改为「允许的价态列表」，并在 UI 中让用户切换。

2. **原子量（weight）**
   - **来源**：IUPAC 标准原子量（2021 版）的常用四位有效数字取值。
   - **精度**：足够显示用；`molecularWeight()` 简单累加所有原子（含氢）的原子量。
   - **⚠️ 已知简化**：这是**平均原子量**（考虑天然同位素丰度），不是单一同位素的精确质量（monoisotopic mass）。质谱场景需换用后者。

3. **绘制颜色**
   - **来源**：**CPK 配色法**（Corey–Pauling–Koltun colour convention），化学可视化的事实标准，被 PyMOL / Jmol / RDKit / Avogadro 等广泛采用。
   - CPK 标准色：C 黑、O 红、N 蓝、S 黄、P 橙、H 白。
   - **本项目的调整**：因界面为**白底**，把 CPK 原色统一**降低饱和度并调暗**，使其在白背景上不刺眼；H 由白改为浅灰（白底上白色不可见）。
   - **代码位置**：颜色常量直接写在 `elementInfo()` 表中。

4. **绘制半径**
   - **来源**：**定性参考**共价半径 / 范德华半径的相对大小趋势（S > P > C > N > O > H）。
   - **⚠️ 重要说明**：这些数值是**为视觉效果手调的像素值，不是真实原子半径的等比缩放**。真实共价半径约为 C 77 pm、O 66 pm、N 71 pm、S 105 pm、P 107 pm、H 31 pm。本项目只保留大小排序，具体比例按"看起来舒服"调整。

### 7.2 成键规则

**代码位置**：`Molecule::addAtom()` / `heavyBondOrderSum()` / `maxOrderForBond()`

| 规则 | 实现 | 来源与简化说明 |
| --- | --- | --- |
| 剩余价键计算 | `valence - heavyBondOrderSum(i)` | 基础价键理论。注意分母**只算重原子键**，氢被视为"可被替换的占位" |
| 新原子的连接点 | 剩余价键 > 0 且离点击点最近的重原子 | **纯交互设计，无化学依据**。真实化学中新键的位置由反应机理决定 |
| 键级上限 | `min(freeA, freeB)`，裁剪到 [1,3] | 价键守恒。三键上限对应 C≡C / C≡N |
| 氢键恒为单键 | `maxOrderForBond` 遇氢直接返回 1 | 氢只有 1 个价电子，无法成多重键（正确） |

**⚠️ 未实现的化学概念**：
- **芳香性**（苯环的离域 π 键）—— 苯会被表示为交替单双键（Kekulé 结构），不做芳香判定
- **环的检测与张力** —— 力导向布局不识别环，环的几何完全由弹簧与斥力自发形成
- **立体化学** —— 无手性、无顺反异构（E/Z）、无楔形键
- **共振结构** —— 如羧基 –COOH 的两个 C–O 键实际等价，本项目视为一个单键一个双键
- **形式电荷与离子** —— 不支持
- **配位键 / 氢键（分子间作用力）** —— 不支持（注意：本项目里的 "kBondHydrogen" 指"连接氢原子的共价键"的绘制颜色，**不是**化学上的氢键 hydrogen bond）

### 7.3 氢原子自动补齐

**代码位置**：`Molecule::rebuildHydrogens()`

**算法**：
1. 清空所有现有氢原子及含氢的键
2. 对每个重原子：`nH = max(0, valence - heavyBondOrderSum(i))`
3. 计算氢的摆放方向：
   - 累加所有已有邻居的单位方向向量得到 `occupied`
   - 基准角 = `atan2(-occupied.y, -occupied.x)`（背离邻居的方向）
   - 多个氢以 `spread = 2π / (nH + neighbours + 1)` 为间隔在基准角两侧张开

**来源与依据**：
- **补氢数量**：完全由价键规则决定，这是化学正确的（对不带电荷的中性分子）。等价于 SMILES 中的「隐式氢」（implicit hydrogen）概念，RDKit 的 `Chem.AddHs()` 做的是同一件事。
- **⚠️ 摆放角度是纯几何近似，非 VSEPR**：
  - 真实的 sp³ 碳键角为 **109.5°**（四面体），sp² 为 **120°**（平面三角），sp 为 **180°**（直线）。这由 **VSEPR 理论**（价层电子对互斥理论）决定。
  - 本项目**不做杂化判定**，只用"背离已有邻居 + 均匀张开"的二维启发式。在二维平面上四面体角本就无法正确表达。
  - 最终角度还会被力导向的斥力进一步调整，所以显示出的角度是弹簧-斥力平衡的结果，**不是化学键角**。
- **若需正确键角**：需引入杂化态判定（按 σ 键数 + 孤对电子数）+ 三维坐标 + 真实力场（MMFF94 / UFF）。这正是 Avogadro2 `avogadro::calc` 模块的职责。

### 7.4 结构式生成

**代码位置**：`Molecule::structuralFormula()` + `longestChain()` + `hasRing()` + `ringStructuralFormula()` + `canonicalSmiles()`

**入口分流**：`structuralFormula()` 先调用 `hasRing()` 判断分子是否含环，再分两条路。

#### 7.4.1 无环分子：凝聚式结构式
1. **求最长链**：`longestChain()` 用**双 BFS 求树直径**的经典算法
   - 从任意重原子 BFS → 找到最远点 A
   - 从 A 再 BFS → 找到最远点 B
   - A→B 即为最长路径（该算法**仅对树严格正确**，故只用于无环分支）
2. **渲染每个原子**：`C` + 氢数 → `CH3` / `CH2` / `CH`
3. **支链**：不在主链上的重原子邻居用括号表示 `(CH3)`
4. **键符号**：单键 `-`、双键 `=`、三键 `#`

**输出示例**：乙醇 → `CH3-CH2-OH`；丙烯 → `CH3-CH=CH2`；乙炔 → `CH#CH`；异丁烷 → `CH3-CH(CH3)-CH3`

#### 7.4.2 含环分子：SMILES 环结构式
1. **环检测** `hasRing()`：利用「连通图是树 ⟺ 边数 = 顶点数 − 1」的性质，统计重原子数 N 与重原子间键数 M，当 `M >= N`（且 `N >= 2`）即含环（双/三键按 1 条边计连通性）
2. **环结构式** `ringStructuralFormula()`：两遍 DFS
   - 第一遍 DFS 用颜色标记找 **back edge**（灰色祖先），每条 back edge 对应一个环，记录打开端（祖先）、闭合端（后代）与闭合键，按发现顺序分配环编号
   - 第二遍 DFS 输出：原子符号后接打开/闭合环编号，环闭合键跳过，多重键用 `=` / `#`，单键省略，分支用 `(...)` 括号
3. **隐式氢**：环上碳的氢省略（SMILES 惯例），避免 `[CH]1=[CH]...` 的冗杂形式

**输出示例**：苯环 → `C1=CC=CC=C1`；环己烷 → `C1CCCCC1`

#### 7.4.3 规范 SMILES（供常用名匹配）
`canonicalSmiles()` 生成**确定性、图同构不变**的 SMILES（Kekulé、隐式氢）：
- **起始原子**：取不变量字典序最小（`度数 → 原子序数 → 键级列表 → 氢数`），端点优先
- **邻居顺序**：键级降序 → 原子序数升序 → 索引升序
- **环**：与 `ringStructuralFormula()` 相同的 DFS 回边检测 + 环编号

无论用户从哪个原子开始画、双键如何交替（苯环两种 Kekulé 式），输出都一致，用于与内置物质表精确比对。

#### 7.4.4 常见物质数据库（常用名）
- **数据**：`MoleculeModel.cpp` 的 `kCommonSubstances[]`，26 个常见物质的 `canonical SMILES → 英文常用名`
- **查询**：`commonName()` 用 `canonicalSmiles()` 的输出去查表，命中即返回英文常用名，未命中返回空
- **显示**：`MoleculeCanvas::paintStructuralFormula()` 识别到常用名时，在结构式上方加粗显示 `COMMON NAME`
- **收录示例**：Water `O`、Methane `C`、Carbon dioxide `O=C=O`、Benzene `C1=CC=CC=C1`、Methanol `CO`、Ethanol `CCO`、Acetic acid `CC=O(O)`、Cyclopropane~Cyclohexane 等
- **⚠️ 边界**：多环 + 杂原子桥接的芳香族（如二苯醚 Phenyl ether）暂未收录，因手写 canonical SMILES 易错；引入 RDKit 后可直接补齐（见 §7.6 升级路径）

**来源与依据**：
- **凝聚式结构式**（condensed structural formula）是标准的化学表示法，把每个碳及其氢写在一起，用线表示碳骨架。
- **SMILES**（Simplified Molecular-Input Line-Entry System）是化学信息学的标准线性记法，环用闭合编号表示。本项目借用其记法，但**未实现完整 SMILES 语义**（无芳香性、无电荷、无立体化学）。
- 三键用 `#` 而非 `≡`：SMILES 约定（SMILES 用 `#` 表示三键），纯 ASCII 输出避免字体缺字。
- **⚠️ 与 IUPAC 命名的区别**：本实现只做"沿最长链平铺"或"环编号展开"，**不遵循 IUPAC 主链选择规则**（IUPAC 要求主链最长、取代基编号最小、官能团优先级等）。它是一个**可读的拓扑摘要**，不是规范命名。

**Hill 式分子式**（`formula()`）：
- **来源**：**Hill 系统**（Edwin A. Hill, 1900），化学文摘的标准排序法。
- 规则：含碳化合物 → C 在最前、H 第二、其余元素按字母序。本项目正确实现了这一点。
- 例：乙醇 `C2H6O`（不是 `C2H5OH`——后者是结构式写法，见 `structuralFormula()`）

### 7.5 几何布局：VSEPR 键角力场（v0.11 重写）⭐

**代码位置**：`Molecule::relaxStep()` + `Molecule::stericNumber()` + `Molecule::idealBondAngle()`

#### 7.5.1 为什么要重写

v0.10 之前只有「键长弹簧 + 全局斥力」两项力，**没有键角项**。后果是：斥力总是把相邻原子推到能推的最远处，于是每个键角都趋向 180°，**丁烷被画成一条直线而不是锯齿**。这是纯图可视化算法（Fruchterman–Reingold 类）用在分子上的典型失真。

v0.11 把它改成一个**微型分子力学力场**，结构与 MMFF94 / UFF 相同（少了三维和二面角项）。

#### 7.5.2 受力模型

| # | 力 | 公式 | 参数 | 化学依据 |
| --- | --- | --- | --- | --- |
| 1 | 键伸缩 | `F = (len − ideal) · kSpringK` | `kSpringK = 0.055` | 胡克定律。`ideal` 重原子间 62 px、C–H 34 px |
| 1b | 键级 → 键长 | 双键 × 0.88、三键 × 0.79 | — | ✅ 真实键长 C–C 154 pm、C=C 134 pm（比 0.87）、C≡C 120 pm（比 0.78） |
| 2 | **键角弯曲（新）** | `E = ½k(θ − θ₀)²`，`F = k(θ−θ₀)·t̂/r` | `kAngleHeavyK = 150`、`kAngleHydrogenK = 55` | ✅ **VSEPR**，θ₀ 由杂化态决定（见下） |
| 3 | 非键斥力 | `F = k / max(d², 36)` | `kRepulsionK = 3000`；含氢 × 0.35 | ✅ **排除 1-2 / 1-3 对**，与真实力场一致 |
| 4 | 质心回中 | `F = −centroid · kCentringK` | `kCentringK = 0.004` | ❌ 纯 UI 需求，不改变分子形状 |
| 5 | 阻尼 / 限速 | `v *= 0.86`，`maxSpeed = 9` | — | 数值稳定 |

#### 7.5.3 理想键角从哪来（VSEPR）

`stericNumber(i)` = **σ 键数 + 孤对电子数**，其中

```
孤对数 = (元素价电子数 − 该原子所有键级之和) / 2
```

价电子数取自 `ElementInfo::valenceElectrons`（C 4、N/P 5、O/S 6、H 1）。注意 **π 键不单独占据电子域**，所以双键三键只算一个 σ。

| 杂化 / 空间构型 | 立体数 SN | θ₀ | 实例 |
| --- | --- | --- | --- |
| 直线形 sp | 2 | **180°** | HC≡CH、O=C=O |
| 平面三角 sp² | 3 | **120°** | H₂C=CH₂、苯环 |
| 四面体 sp³ | 4 | 见下表（按元素） | CH₄、H₂O、NH₃ |

**孤对也算进 SN**，这正是水和醚呈弯曲而非直线的原因：O 有 2 个 σ 键 + 2 对孤对 → SN=4。

#### 7.5.3b sp³ 键角按元素取实验值（v0.12 新增）⭐

**代码位置**：`experimentalBondAngleDegrees()`（`MoleculeModel.h`）

v0.11 对所有 SN=4 的中心一律用 109.47°，结果只有碳看起来对。真实键角**强烈依赖中心元素**——孤对占据的电子域比成键域"胖"，把成键角往下压，越往下周期压得越狠（Bent 规则 / 孤对–成键对斥力）：

| 中心 | 两侧都是重原子 | 参照物种 | 两侧含氢 | 参照物种 |
| --- | --- | --- | --- | --- |
| C sp³ | **112.4°** | 丙烷 C–C–C | 109.5° | 甲烷 H–C–H |
| N sp³ | **110.9°** | 三甲胺 C–N–C | 107.8° | 氨 H–N–H |
| O sp³ | **111.7°** | 二甲醚 C–O–C | 104.5° | 水 H–O–H |
| S sp³ | **99.1°** | 二甲硫醚 C–S–C | 92.1° | 硫化氢 H–S–H |
| P sp³ | **98.6°** | 三甲基膦 C–P–C | 93.5° | 膦 H–P–H |

数据来源：CRC Handbook of Chemistry and Physics 气相结构数据；Allen et al., *Tables of bond lengths and angles*, J. Chem. Soc. Perkin Trans. II (1987)。

sp² / sp 中心与元素无关（120° / 180°），所以只有 sp³ 这一行需要分元素。

> **常见误解澄清**：**C–O–C 不是直线**。醚氧上有两对孤对占据电子域，SN=4，二甲醚的实测 C–O–C = 111.7°——几乎和碳链的 112.4° 一样弯。会觉得它"应该是直线"通常是把它和 **O=C=O（二氧化碳）** 混淆了：那里的中心是碳，SN=2，才是真正的 180°。本项目里画 `C=O=C` 是不可能的（氧只有 2 价），而 `O=C=O` 会正确地呈现为直线。真正肉眼可辨的差别在**硫和磷**：C–S–C 只有 99°，画出来明显比碳链尖。

#### 7.5.4 两遍角度求解

在二维平面上，一个原子的所有邻居必须共享 360°，无法让 4 个邻居两两都成 109.5°。因此分两遍处理：

- **Pass A（骨架，刚性 k=150）**：只取重原子邻居。恰好 2 个时直接用 θ₀；≥3 个时目标取 `min(θ₀, 360°/n)`。
- **Pass B（氢，柔性 k=55）**：全部邻居按极角排序，只对**含氢**的相邻对施力，目标 `min(θ₀, 360°/n)`。

结果：丁烷主链 C–C–C 稳定在 109.5° 锯齿，氢原子填满剩余角度——这正是化学教科书里的画法。

#### 7.5.5 关键改动：1-2 / 1-3 排除

真实力场**不计算成键原子（1-2）和键角两端原子（1-3）之间的范德华力**，因为这些距离已由键长项和键角项确定。本项目照做：

```cpp
for (bonds)      exclude(a, b);              // 1-2
for (each atom)  exclude(每对邻居);           // 1-3
```

- 不排除的话，斥力会持续对抗角度项，把角度撑平
- **1-4 及更远的对仍然保留斥力**——而这恰恰是长链自发选择**反式锯齿（anti）构象**而非蜷曲的原因，与真实构象分析的结论一致

#### 7.5.6 退化情况处理

当 θ = 180° 时角度梯度为零（两个邻居向量反平行），是一个鞍点，普通梯度下降会卡死。代码检测到这种情况时，把两个邻居**朝同一侧**垂直推一下打破对称：

```cpp
// 反向推会让它们保持反平行，必须同向推
force[p1] += perp * kick;
force[p2] += perp * kick;
```

同时 `addAtom()` 也改进了：新原子直接落在**理想键角方向**（在指针所在的那一侧），而不是指针方向，从源头避免共线起步。

#### 7.5.6b 环几何：多边形内角 + 环模板（v0.12 新增）⭐

**代码位置**：`Molecule::ringAngleTarget()`、`relaxStep()` 的 2b 段、`Molecule::rings()`

v0.11 引入键角后出现了两个新缺陷，根因是同一个：

**缺陷 A —— 成环时出现凹角多边形。**
一个闭合的平面 n 边形，内角和恒为 `(n−2)·180°`，即每个顶点 `180° − 360°/n`。环己烷需要每个顶点 **120°**，但一个孤立 sp³ 碳想要 112.4°。目标角与几何约束互相矛盾，求解器只能靠**把某一个顶点翻成凹角**来凑齐周长，于是环被"别"出一个坑。

修法：`ringAngleTarget(centre, n1, n2)` 判断两个邻居是否是 `centre` 在同一个环里的前驱/后继，是的话把目标角换成多边形内角：

| 环 | n | 内角目标 | 真实值 |
| --- | --- | --- | --- |
| 环丙烷 | 3 | 60° | ✅ 60°（真实就是 60°，张力环） |
| 环丁烷 | 4 | 90° | ✅ ~88°（略皱折） |
| 环戊烷 | 5 | 108° | ✅ ~104°（信封构象） |
| 苯 / 环己烷 | 6 | 120° | ✅ 苯 120°；环己烷椅式实为 111°，二维只能画成 120° |

**缺陷 B —— 关掉界面再打开，苯环变成心形。**
`acos()` 给出的是**无符号夹角**：一个"翻到反面"的顶点（实际 240°）和正常顶点（120°）的能量完全相同。这个镜像简并在增量绘制时不会被踩到，但从持久化状态恢复（无坐标）时求解器有一半概率落进去。

修法有两层：

1. **环模板力**（`kRingTemplateK = 0.10`）：对每个识别出的环，把成员原子拉到「同边长正多边形外接圆」上，半径 `R = L / (2 sin(π/n))`（`L` 取环内平均理想键长，双键短 12%）。圆上的点天然是凸的，镜像解被直接消掉。这也正是所有结构式绘制软件的做法——RDKit 的二维坐标生成器同样是**先摆环模板、再松弛取代基**。
2. **`seedLayout()` 环感知初始布局**：`fromValueTree()` 不再把原子无脑铺一圈，而是先把环摆成正多边形（稠环按共享边镜像翻折，如萘），再 BFS 沿理想键角把链和取代基长出去。求解器拿到的已经是几乎正确的构型，只需微调。

**环缓存**：`perceiveRings()` 每条键跑一次 BFS，不能每帧算。`rings()` 用 `topologyVersion` 计数器做记忆化，只在 `rebuildHydrogens()`（所有拓扑变更的必经之路）里失效一次。重原子索引恒为 `0..heavyCount−1`、氢追加在后，所以缓存里存裸索引是安全的。

#### 7.5.7 与真实力场仍有的差距

| 项 | 状态 |
| --- | --- |
| 键伸缩项 | ✅ 有 |
| 键角弯曲项 | ✅ 有（VSEPR 骨架 + 元素实验值） |
| 1-2/1-3 排除 | ✅ 有 |
| 环几何 | ✅ 多边形内角目标 + 环模板（v0.12） |
| 孤对的额外压缩效应 | ✅ 有（v0.12 起按元素查实验值，H₂S 92°、PH₃ 93.5° 等） |
| 二面角 / 扭转项 | ❌ 无（二维无二面角概念） |
| Lennard-Jones 势 | ❌ 用简化反平方斥力代替 |
| 静电 / 部分电荷 | ❌ 无 |
| 三维构象 | ❌ 纯二维（环己烷椅式只能画成正六边形） |
| 显式环张力能量 | ❌ 无（几何正确但不给出张力数值） |

**升级路径**：Avogadro2 的 `avogadro::calc::EnergyCalculator`（MMFF94 / UFF）可整体替换本函数。

### 7.6 外部库调研记录

开发期间实际拉取并编译验证了以下库，结论见 §1.4。此处记录技术细节备查。

**RDKit**（[github.com/rdkit/rdkit](https://github.com/rdkit/rdkit)，BSD 许可）
- 本地路径：`I:\RDKit`（浅克隆，HEAD `24e5443`）
- 性质：C++ 核心 + Boost.Python 绑定的化学信息学工具包
- 验证方式：装 Python wheel（`rdkit 2026.3.5`）跑通 SMILES 解析、2D 结构式 PNG、子结构匹配、3D 构象生成
- 相关概念对照：
  - RDKit `Chem.MolFromSmiles()` ↔ 本项目无 SMILES 解析
  - RDKit `Chem.AddHs()` ↔ 本项目 `rebuildHydrogens()`
  - RDKit `AllChem.Compute2DCoords()` ↔ 本项目 `relaxStep()`（但 RDKit 用的是真正的 2D 坐标生成算法）
  - RDKit `Descriptors.MolWt()` ↔ 本项目 `molecularWeight()`

**Avogadro2 / avogadrolibs**（[github.com/OpenChemistry/avogadrolibs](https://github.com/OpenChemistry/avogadrolibs)，BSD 许可）
- 本地路径：`I:\Avogadro2\avogadrolibs`（源码）、`I:\Avogadro2\build-core`（编译产物）
- **已验证的最小无 Qt 构建**：
  ```
  -DUSE_QT=OFF -DUSE_OPENGL=OFF -DUSE_HDF5=OFF -DUSE_LIBARCHIVE=OFF
  -DUSE_LIBMSYM=OFF -DUSE_SPGLIB=OFF -DUSE_PLOTTER=OFF -DUSE_MMTF=OFF
  -DUSE_PYTHON=OFF -DBUILD_SHARED_LIBS=OFF
  ```
  产出：`Core.lib`（分子数据结构）、`IO.lib`（SMILES/Molfile 解析）、`Calc.lib`（力场）、`QuantumIO.lib`
- 依赖：**Eigen 3.3.7**（`I:\Avogadro2\deps\eigen-install`）
- **需打的本地补丁**：`avogadro/io/CMakeLists.txt` 把 PRIVATE 的 `struct` / `pugixml` / `nlohmann_json` 用 `$<BUILD_INTERFACE:...>` 包装，绕过 CMake 4.x 对 `install(EXPORT)` 的严格检查
- **API 要点**：Avogadro2 的 `SmilesFormat` **只写不读**，读取 SMILES 必须用 `Io::SmilesParser::parse(smiles, mol)`
- 冒烟测试：`I:\Avogadro2\apitest`，解析阿司匹林得 21 原子、分子量 180.158（与理论值 180.157 一致）
- GUI 参考：官方安装包装在 `I:\Avogadro2\AvogadroApp`，`bin\avogadro2.exe` 是三维搭分子式的编辑器。本项目的**右键删除原子**交互参考了它，但整体走二维路线
- 完整构建步骤已沉淀为用户级 skill `avogadro2-windows-build`

### 7.7 化学描述符：定义、文献出处与简化程度 ⭐（v0.11 新增）

**代码位置**：`Molecule::computeDescriptors()`（`MoleculeModel.cpp`）+ `Molecule::perceiveRings()`

所有描述符均可由二维拓扑推导，对应 RDKit `Descriptors` / `rdMolDescriptors` 模块中的同名量。下表逐条列出**定义、原始文献、本项目的简化**。

#### 7.7.1 组成与拓扑（v0.6 已有）

| 描述符 | 定义 | 正确性 |
| --- | --- | --- |
| `heavyAtomCount` | 非氢原子数 | ✅ |
| `heteroAtomCount` | 非碳重原子数（O/N/S/P） | ✅ |
| `carbonCount` | 碳原子数 | ✅ |
| `ringCount` | 圈秩 = 边数 − 顶点数 + 1（cyclomatic number） | ✅ 连通图成立 |
| `doubleBondCount` / `tripleBondCount` | 重原子间多重键数 | ✅ |
| `longestChainLength` | 双 BFS 求树直径 | ⚠️ 仅对无环严格正确 |
| `branchCount` | 度数 ≥ 3 的重原子数 | ✅ |
| `molecularWeight` | 标准原子量求和 | ✅ 平均原子量 |
| `degreeOfUnsaturation` | 环数 + 双键数 + 2×三键数 | ✅ |

#### 7.7.2 环感知与芳香性（v0.11 新增）

**`perceiveRings()` —— SSSR 近似**
- **算法**：对每根重原子键，删掉它再求两端的最短路径；路径 + 该键即为「过这根键的最小环」。收集去重后得到环集。
- **依据**：这是 SSSR（Smallest Set of Smallest Rings）的经典近似解法。
- **⚠️ 简化**：对稠环体系（如萘、蒽）可能给出与严格 SSSR 不同的环集。本编辑器产出的分子以单环 / 少量稠环为主，实用上足够。

**`aromaticRingCount` —— Hückel 4n+2 规则**
- **依据**：**Hückel's rule**（Erich Hückel, 1931）。平面、完全共轭、π 电子数为 4n+2 的环状体系具有芳香性。
- **实现**：
  ```
  π 电子 = 2 × 环内双键数
         + 2 × (无环内 π 键的杂原子，贡献孤对)
  芳香 ⟺ 全环可平面 sp² 且 (π − 2) mod 4 == 0
  ```
- **验证**：
  - 苯 C₆H₆：3 个环内双键 → 6 π e → ✅ 芳香
  - 吡啶（含 N 的六元环）：6 π e → ✅ 芳香
  - 呋喃（含 O 的五元环，2 个双键）：4 + O 孤对 2 = 6 π e → ✅ 芳香
  - 环己烷：0 π e → ❌ 非芳香（正确）
  - 环丁二烯：4 π e → ❌ 反芳香（正确排除）
- **⚠️ 简化**：只检查 3~8 元环；不做严格平面性判定；不处理稠环整体离域（萘按两个独立六元环各自判定）。

| 描述符 | 定义 |
| --- | --- |
| `maxRingSize` | 最大环的原子数 |

#### 7.7.3 类药性描述符（v0.11 新增）

**`tpsa` —— 拓扑极性表面积**
- **文献**：**Ertl P., Rohde B., Selzer P.**, *"Fast Calculation of Molecular Polar Surface Area as a Sum of Fragment-Based Contributions"*, **J. Med. Chem. 2000, 43, 3714–3717**.
- **实现**：按原子环境查片段贡献表并求和。本项目实现的片段（单位 Å²）：

  | 原子环境 | 贡献 | | 原子环境 | 贡献 |
  | --- | --- | --- | --- | --- |
  | N 芳香（无 H） | 12.89 | | O 醚 −O− | 9.23 |
  | N 芳香（带 H） | 15.79 | | O 羟基 −OH | 20.23 |
  | N 叔胺 | 3.24 | | O 羰基 =O | 17.07 |
  | N 仲胺 | 12.03 | | O 芳香 | 13.14 |
  | N 伯胺 | 26.02 | | S 硫醚 −S− | 25.30 |
  | N 亚胺 =N− | 12.36 | | S 硫醇 −SH | 38.80 |
  | N 腈 ≡N | 23.79 | | S 硫酮 =S | 32.09 |
  | P 三价 | 13.59 | | P 带 H | 23.47 |

- **⚠️ 简化**：原文含 43 个片段类型，本项目实现约 16 个（覆盖 C/O/N/S/P 调色板的常见情形）。带电荷氮、季铵盐等未实现。

**`clogP` —— Crippen 原子贡献法亲脂性**
- **文献**：**Wildman S.A., Crippen G.M.**, *"Prediction of Physicochemical Parameters by Atomic Contributions"*, **J. Chem. Inf. Comput. Sci. 1999, 39, 868–873**.
- **实现**：按原子类型累加贡献值。本项目采用的代表值：

  | 原子类型 | 贡献 | | 原子类型 | 贡献 |
  | --- | --- | --- | --- | --- |
  | C 脂肪族 | +0.1441 | | O 羟基 | −0.2893 |
  | C 芳香 | +0.1581 | | O 醚 | +0.1129 |
  | C 连杂原子 | −0.2035 | | O 羰基 | −0.1526 |
  | C 不饱和 | 0.0000 | | S | +0.6482 |
  | N 胺（带 H） | −1.0190 | | P | −0.3260 |
  | N 芳香 | −0.3187 | | H 连碳 | +0.1230 |
  | N sp²（=N−） | −0.7096 | | H 连杂原子 | −0.2677 |

- **⚠️ 重要简化**：原文定义 **68 个原子类型**，本项目只区分约 **14 个大类**，每类取该类的代表值。**结果只能视为亲脂性的定性趋势，不是可用的 logP 预测值。** 真实用途请用 RDKit `Crippen.MolLogP()`。

**`rotatableBondCount` —— 可旋转键**
- **文献**：**Veber D.F. et al.**, *"Molecular Properties That Influence the Oral Bioavailability of Drug Candidates"*, **J. Med. Chem. 2002, 45, 2615–2623**.
- **定义**：单键、非环、两端重原子的度数均 ≥ 2（排除端基甲基这类不产生新构象的键）。
- **⚠️ 简化**：未排除酰胺 C–N 键（真实定义中酰胺键因共振而不可自由旋转）。

**`hBondDonorCount` / `hBondAcceptorCount` —— Lipinski 氢键计数**
- **文献**：**Lipinski C.A. et al.**, *"Experimental and computational approaches to estimate solubility and permeability..."*, **Adv. Drug Deliv. Rev. 1997, 23, 3–25**（Rule of Five）。
- **定义**：供体 = N–H 与 O–H 的氢原子数；受体 = N 与 O 原子数。
- **⚠️ 简化**：这是 Lipinski 的**宽松计数版本**；更严格的定义会排除酰胺氮、吡咯氮等。

**`fractionSp3` —— Fsp3 饱和度**
- **文献**：**Lovering F., Bikker J., Humblet C.**, *"Escape from Flatland: Increasing Saturation as an Approach to Improving Clinical Success"*, **J. Med. Chem. 2009, 52, 6752–6756**.
- **定义**：sp³ 碳数 / 总碳数。本项目用 `stericNumber(i) >= 4` 判定 sp³。
- **正确性**：✅ 与 RDKit `Descriptors.FractionCSP3()` 一致。

#### 7.7.4 拓扑指数（v0.11 新增）

**`wienerIndex` —— Wiener 指数**
- **文献**：**Wiener H.**, *"Structural Determination of Paraffin Boiling Points"*, **J. Am. Chem. Soc. 1947, 69, 17–20**。化学信息学史上第一个拓扑指数。
- **定义**：所有重原子对之间**最短路径长度之和**（每对计一次）。
- **实现**：对每个重原子做一次 BFS 求单源最短路，累加。
- **含义**：线性长链的 Wiener 值远大于同原子数的紧凑环状结构，因此它衡量「骨架铺展程度」。

**`randicIndex` —— Randić 支化指数（¹χ）**
- **文献**：**Randić M.**, *"Characterization of Molecular Branching"*, **J. Am. Chem. Soc. 1975, 97, 6609–6615**.
- **定义**：`¹χ = Σ_bonds 1/√(dᵢ·dⱼ)`，d 为重原子度数。
- **含义**：分子连接性指数，反映骨架的支化与连接模式。

**`bondPolarity` —— 键极性总和**
- **定义**：所有键（含 C–H）两端**Pauling 电负性差的绝对值之和**。
- **电负性数据**（`ElementInfo::electronegativity`，Pauling 标度）：H 2.20、C 2.55、N 3.04、O 3.44、S 2.58、P 2.19。
- **⚠️ 简化**：这**不是真正的偶极矩**。真实偶极矩是矢量和，需要考虑几何对称性（例如 CO₂ 两个 C=O 极性键因线性对称而净偶极为零，本指标却会给出较大值）。本项目仅用它作为「分子内极性键多寡」的粗略标量。

#### 7.8 化学正确性总结

| 方面 | 状态 |
| --- | --- |
| 价键守恒（不超价） | ✅ 正确（在 2/3/4 价的简化设定下） |
| 隐式氢数量 | ✅ 正确（中性分子） |
| Hill 式分子式 | ✅ 正确 |
| 分子量 | ✅ 正确（平均原子量） |
| 键级 → 键长趋势 | ✅ 定性正确 |
| CPK 配色 | ✅ 遵循（降饱和适配白底） |
| **键角（VSEPR）** | **✅ v0.11 起正确**（sp/sp²/sp³，孤对计入立体数） |
| **sp³ 键角按元素取实验值** | **✅ v0.12 起正确**（C 112.4°/N 110.9°/O 111.7°/S 99.1°/P 98.6°，含氢时取氢化物值） |
| **环内键角 = 多边形内角** | **✅ v0.12 起正确**（环己烷 120°、环丙烷 60°，不再出现凹角） |
| **芳香性（Hückel 4n+2）** | **✅ v0.11 起实现**（苯/吡啶/呋喃正确，环丁二烯正确排除） |
| **环感知（SSSR）** | ✅ 近似正确（稠环可能偏离严格 SSSR） |
| **Fsp3** | ✅ 与 RDKit 一致 |
| **Wiener / Randić 指数** | ✅ 定义正确 |
| **TPSA** | ⚠️ 片段表为原文子集（16/43） |
| **cLogP** | ⚠️ 原子类型大幅简化（14/68），仅定性趋势 |
| **可旋转键** | ⚠️ 未排除酰胺键 |
| **氢键供受体** | ⚠️ Lipinski 宽松计数 |
| `bondPolarity` | ⚠️ 标量近似，非真实偶极矩（忽略对称性） |
| 原子半径比例 | ❌ 手调像素值，仅保留大小排序 |
| 立体化学 | ❌ 未实现 |
| 多价态（S 4/6 价、P 5 价） | ❌ 未实现 |
| 形式电荷 / 离子 | ❌ 未实现 |
| IUPAC 命名 | ❌ 结构式仅为拓扑摘要 |
| 三维构象 / 二面角 | ❌ 纯二维 |

### 7.9 分子预设库（v0.12 新增）⭐

**代码位置**：`organic::moleculePresets()` / `organic::presetValueTree()`（`MoleculeModel.cpp`），UI 在 `PluginEditor::showPresetMenu()`

**为什么用拓扑直写而不是 SMILES**：本项目只有 SMILES **writer**，没有 parser（见 §1.4）。为了几个预设去写解析器不划算，所以预设直接以「元素数组 + 键三元组」存放，正好就是 `toValueTree()` 的数据形状，可以走已有的持久化通路：`presetValueTree(i)` → `MoleculeCanvas::restoreMolecule()` → `Molecule::fromValueTree()` → `seedLayout()` → 补氢。

**编写约束**：

- 芳香环一律写 **Kekulé 式**（单双键交替）。本项目没有芳香键类型，芳香性是由 `computeDescriptors()` 用 Hückel 规则**识别**出来的，不是存出来的。
- 每个原子的键级之和不得超过本项目设定的中性价（C 4 / N 3 / O 2 / S 2 / P 3），否则补氢会算成负数。表中每一条都已逐原子核对。

**当前 33 条**：

| 分组 | 条目 |
| --- | --- |
| 一~二重原子 | Methane、Methanol、Formaldehyde、Ethane、Ethylene、Acetylene |
| 短链与官能团 | Ethanol、Acetaldehyde、Dimethyl ether、Dimethyl sulfide、Trimethylphosphine、Propane、Acetonitrile、Acetone、Acetic acid、Urea、Butane、Isobutane、Ethylene glycol、Glycine |
| 饱和环 | Cyclopropane、Cyclopentane、Cyclohexane |
| 芳香环 | Benzene、Toluene、Phenol、Aniline、Styrene、Pyridine、Furan、Thiophene |
| 稠环 / 大分子 | Naphthalene、Glucose (open)、Caffeine |

其中 **Dimethyl ether / Dimethyl sulfide / Trimethylphosphine** 是刻意放进来的键角对照组：同为 SN=4，画出来分别是 111.7° / 99.1° / 98.6°，一眼能看出元素差异。**Naphthalene** 用来验证稠环模板的镜像展开，**Caffeine** 用来验证多环 + 多杂原子下补氢是否仍然正确。

**交互**：顶栏分子式本身就是入口，左右 `<` `>` 循环切换。用户一旦手动改动分子，
`currentPreset` 立即置 −1，抬头不再声称自己是某个预设。

**为什么自己画下拉面板（v0.13）**：`juce::PopupMenu` 走宿主的 LookAndFeel，弹出来是一块
系统风格的灰蓝菜单，和这个插件"白底 / 细描边 / 无渐变"的语言完全不搭，而且在不同 DAW 里
长相还不一样。`PresetMenu`（`PresetMenu.h/.cpp`）自己画：

- 组件铺满整个编辑器充当**遮罩层**，点卡片外任意位置即关闭（不需要额外的焦点管理）
- 卡片固定 **3 列**，行数由条目数决定，33 条一屏放得下，**不需要滚动**
- **逐列填充**（先填满第一列再第二列）——眼睛从上往下扫比从左往右扫快
- 每行左侧英文名、右侧灰色分子式（`presetFormula(i)`，首次调用时批量算好并缓存）
- 选中项用左侧一条 2 px 强调竖条，不用勾选符号（更克制）
- 卡片投影用三层递减的半透明圆角矩形叠出来，比 `DropShadow` 更可控也更干净
- 抬头去掉了倒三角，改成悬停时出现的一条细下划线

### 7.10 常用名库（v0.13 重写）⭐

**代码位置**：`Molecule::commonName()` + `organic::namedMolecules()`

**原来的问题**：常用名表是一张**手写的 SMILES 字符串**表。它会静默腐烂——用户能从预设加载
一个分子，回来却显示不出名字，因为手写的 SMILES 和 `canonicalSmiles()` 实际产出的字符串
对不上（环闭合编号、分支顺序、遍历起点都可能不同，人很难凭空写对）。表里原本
`"CC=O(O)"` 这种写法就是错的，永远匹配不上。

**现在的做法**：名字表在运行时**反算**出来。`commonName()` 首次被调用时遍历
`namedMolecules()`，把每条拓扑真的构造成一个 `Molecule`，问它自己的 `canonicalSmiles()`，
用结果做 key 建索引（函数内 `static`，C++11 magic static 保证线程安全，只算一次）。

这样**预设一定能被命名**——两者用的是同一份拓扑、同一个 writer，不可能对不上。手写字符串
表只保留给几个不值得写拓扑的小无机物（水、CO₂、N₂、O₂、H₂O₂、SO₂、CS₂、NH₃、PH₃、H₂S）。

**收录范围（78 条）**：

| 来源 | 数量 | 内容 |
| --- | --- | --- |
| `moleculePresets()` | 33 | 见 §7.9，下拉面板里的全部条目 |
| `namedMolecules()` 扩展段 | 45 | 烯烃炔烃、醇（含甘油、叔丁醇）、羧酸（甲酸/丙酸/丁酸/丙烯酸/草酸/丙二酸/丁二酸/富马酸/乳酸/丙酮酸）、酯与酸酐、酰胺与胺、硫醇、醚与饱和杂环（THF / 二噁烷 / 哌啶 / 吗啉 / 环氧乙烷）、碳环（环丁烷 / 环庚烷 / 环己烯 / 环己醇 / 环己酮）、芳香杂环（吡咯 / 咪唑 / 嘧啶）与取代苯（苯甲醛 / 苯甲酸 / 苯甲醚 / 对二甲苯 / 对甲酚） |
| 手写 SMILES 表 | 10 | 小无机物 |

**加名字的正确姿势**：往 `namedMolecules()` 的 `extras` 里加一条拓扑即可，**不要**手写 SMILES。
逐原子核对价键不超限（C 4 / N 3 / O 2 / S 2 / P 3），芳香环写 Kekulé 式。

---

## 8. 构建与运行

### 8.1 构建命令（Windows / Ninja）

本机 CMake 4.3.3 + VS18 组合下，**"Visual Studio 18 2026" generator 会在配置阶段 Access violation**，必须用 Ninja。

**⚠️ `Enter-VsDevShell` 已不可用**（见 §8.3）。改为直接设置工具链环境变量。以下是在 Git Bash 中的可用流程：

```bash
VS="/c/Program Files/Microsoft Visual Studio/18/Professional"
SDK="/c/Program Files (x86)/Windows Kits/10"
MSVC="$VS/VC/Tools/MSVC/14.51.36231"
SDKV="10.0.28000.0"

export INCLUDE="$(cygpath -w "$MSVC/include");$(cygpath -w "$SDK/Include/$SDKV/ucrt");\
$(cygpath -w "$SDK/Include/$SDKV/shared");$(cygpath -w "$SDK/Include/$SDKV/um");\
$(cygpath -w "$SDK/Include/$SDKV/winrt");$(cygpath -w "$SDK/Include/$SDKV/cppwinrt")"

export LIB="$(cygpath -w "$MSVC/lib/x64");$(cygpath -w "$SDK/Lib/$SDKV/ucrt/x64");\
$(cygpath -w "$SDK/Lib/$SDKV/um/x64")"

export PATH="$MSVC/bin/Hostx64/x64:$SDK/bin/$SDKV/x64:$PATH"

# 配置（复用已有 JUCE 源码，避免重复下载）
"/c/Program Files/CMake/bin/cmake.exe" \
  -S "I:\\Organic Chemistry" -B "I:\\Organic Chemistry\\cmake-build-ninja" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl \
  "-DFETCHCONTENT_SOURCE_DIR_JUCE=I:\\CRTLoss\\cmake-build-release-visual-studio\\_deps\\juce-src"

# 编译
"/c/Program Files/CMake/bin/cmake.exe" --build "I:\\Organic Chemistry\\cmake-build-ninja"
```

同样逻辑的 PowerShell 版本在 [`build.ps1`](/I:/Organic%20Chemistry/build.ps1)（自动探测最新 MSVC / SDK 版本）。注意本机 PowerShell 执行策略默认禁止运行 .ps1，需 `-ExecutionPolicy Bypass` 或把内容内联执行。

### 8.2 产物

```
cmake-build-ninja\OrganicChemistry_artefacts\Release\
├── Standalone\Organic Chemistry.exe      独立应用（约 4.6 MB）
└── VST3\Organic Chemistry.vst3           VST3 插件
```

`COPY_PLUGIN_AFTER_BUILD TRUE` 会自动把 VST3 拷到系统插件目录。

### 8.3 已知构建坑

| 现象 | 原因 | 解法 |
| --- | --- | --- |
| CMake 配置报 `Access violation` | CMake 4.3.3 + VS18 的 "Visual Studio" generator 不兼容 | 换 Ninja generator |
| **`Enter-VsDevShell` 报 `Item has already been added. Key in dictionary: 'http_proxy'`** | 环境里同时存在 `http_proxy` 和 `HTTP_PROXY`（大小写重复），DevShell 模块建**大小写不敏感**字典时冲突。更糟的是它会把 PATH 写坏一半，连 `findstr` 都找不到 | 别用 `Enter-VsDevShell`，直接设 `INCLUDE` / `LIB` / `PATH`（见 §8.1）。或先 `Remove-Item Env:http_proxy` |
| **`add_subdirectory given source "modules" which is not an existing directory`** | `cmake-build-release-visual-studio/_deps/juce-src` 被清理过，只剩残骸 | 换一份完整的 JUCE 8.0.12 副本，例如 `I:\CRTLoss\cmake-build-release-visual-studio\_deps\juce-src` |
| `error C2228: ".getStringWidth" 的左边必须有类` + `C2737` | **most vexing parse**：`const juce::Font f (juce::FontOptions (14.0f));` 被当成函数声明 | 改大括号初始化：`juce::Font f { juce::FontOptions (14.0f) };` |
| 中文注释编译异常 | MSVC 默认按 GBK 解释 | `target_compile_options(... /utf-8)`（已在 CMakeLists 中） |
| GUI 无法从命令行启动 | 环境限制（Start-Process 后进程不驻留） | 桌面双击运行 |
| `.ps1` 报 `running scripts is disabled` | PowerShell 执行策略 | 内联执行脚本内容，或 `powershell -ExecutionPolicy Bypass -File ...` |

---

## 9. Bell 音色引擎（当前方案）

本节描述把画布上搭建的分子变成可听的钟声。核心是复刻 Vital「BELL Reflections」预设的
三正弦振荡器引擎；分子不再直接决定波形 / 效果参数，而是通过 SMILES 哈希映射到 Bell 引擎的
33 个参数（见 §1.5）。

### 9.1 声音链

```
                     ┌──────────── 每 voice（×8）────────────┐
MIDI ──► note on ──► │  OSC1 分子波表（近正弦，基频）           │
                     │  OSC2 正弦 +12/+24 八度（squeeze 失真）  │
                     │  OSC3 正弦 +12/+24 八度                  │
                     │  → OSC2 经低通滤波（keytrack + ENV2）    │
                     │  → 噪声击打采样（bandpass）              │
                     │  → 幅度包络（钟形）+ ENV2               │
                     └────────────────┬───────────────────────┘
                                      │  混合
     多段压缩 → 合唱（4 声部）→ 延迟（乒乓）→ 混响
        → 降采样失真（BITCRUSH）→ 主电平 → 软削波 → 输出
```

**音频路径**：`processBlock` → `processBellBlock`。旧分子 wavetable DSP 已被 `return` 旁路，
代码仍在但不再执行（见 §9.8）。

#### 各环节实现

| 环节 | 实现 | 说明 |
| --- | --- | --- |
| OSC1 | 分子波表 `buildNearSineWave` | 近正弦（基频 + 少量 SMILES 哈希决定的谐波），随分子编辑更新 |
| OSC2 | 纯正弦 + squeeze 相位失真 | +12/+24 八度，1–7 复音齐奏，经低通 |
| OSC3 | 纯正弦 | +12/+24 八度，最亮高层 |
| 低通 | `juce::dsp::StateVariableTPTFilter` | keytrack 跟随音高 + ENV2 调制截止（E9 溢出已修复，见 §12） |
| 幅度包络 | 逐样本状态机 | 钟形：快起音 + 指数衰减 + 长释放 |
| ENV2 | 同一套状态机 | 推动滤波截止（音头形态） |
| 噪声击打 | noise 采样 → bandpass | 金属击打瞬态，SMILES 哈希选采样 |
| 多段压缩 | `MultibandCompressor` | 三频段（300/3000 Hz 交叉），mix 很小接近旁通 |
| 合唱 | `DelayLine` × 4 + LFO | 4 声部，基础延迟 8/10/12/14 ms，相位各差 90° |
| 延迟 | `DelayLine` × 2 交叉反馈 | 乒乓延迟 |
| 混响 | `juce::dsp::Reverb` | 按块处理 |
| 降采样失真 | `DownsampleDistortion` | BITCRUSH 宏驱动 sample & hold |
| 软削波 | `std::tanh` | 末端软限幅 |

### 9.2 分子 → Bell 参数（`mapMoleculeToBellParams`）

分子不再走「化学性质 → 合成参数」的单调映射，而是：

1. **种子**：`canonicalSmiles()` 经 FNV-1a 哈希 → `xorshift32` PRNG。
2. **确定性**：同一分子永远得到同一串参数（可复现）。
3. **不可预测**：加/删原子让全部参数无规律重排，音色整体换一个。
4. **共性范围**：所有参数落在「冬季钟声/键盘」好音色的范围内（见 §1.5 表格）。

化学描述符 `ChemicalDescriptors`（21 项）仍在 `Molecule::computeDescriptors()` 中计算，
定义与文献出处见 §7.7，但当前 `mapMoleculeToBellParams` **不再使用描述符**（`ignoreUnused(d)`），
只依赖 SMILES 哈希。

### 9.3 波形生成（`buildNearSineWave`）

osc_1 波形是近正弦（不是旧的任意波形）：

1. 种子：SMILES 哈希 → xorshift32。
2. 谐波：h2–h6 各独立随机，偶次（h2）可更强（温暖），奇次克制（避免刺耳）。
3. 总谐波能量约束 ≤ 0.5，保证最坏情况仍是"温暖的正弦"，不会跑偏成锯齿/方波。
4. 峰值归一化到 0.95。

### 9.4 线程安全

- **UI 线程**：`MoleculeCanvas::onMoleculeChanged` → `applyMoleculeToAudio()` → `buildNearSineWave()` + `mapMoleculeToBellParams()` + `hashSmiles()` 选采样 → `setMoleculeWave()` / `setBellParams()` / `setNoiseSampleIndex()`。
- **音频线程**：BellVoice 只读 `getBellParam()` / `getMacro()` / `getOsc1WaveData()` / `getNoiseSampleData()` 等原子/只读快照，不访问 Molecule。
- **Bell 参数**：`std::array<std::atomic<float>, kNumBellParams>`，UI 写、音频读。

### 9.5 涉及文件

| 文件 | 职责 |
| --- | --- |
| `BellEngine.h/.cpp` | `BellVoice`（三正弦 + 包络 + 滤波 + 噪声击打）+ `BellPatch` + `mapMoleculeToBellParams()` + `buildNearSineWave()` + `BellParamId`/`bellParamDef()` |
| `BellWave.h` | osc_1 默认波表常量 `kOsc1Wave` |
| `EnvelopePanel.h/.cpp` | ADSR 图形编辑器（组件已写好，尚未接线/编译） |
| `PluginProcessor.h/.cpp` | `processBellBlock()`（效果链）+ Bell 参数原子数组 + 噪声采样库 + 状态持久化 |
| `PluginEditor.cpp` | `applyMoleculeToAudio()`：分子 → Bell 参数 + osc_1 波表 + 采样 |
| `TestPanel.h/.cpp` | 33 个 Bell 参数调试面板（v1.0.0 起屏蔽） |

### 9.6 DAW 工程持久化

- 分子拓扑（重原子元素 + 键连接 + 键级）经 `Molecule::toValueTree()` 序列化为 `juce::ValueTree`，氢原子是派生数据、恢复时自动重建。
- UI 线程在 `onMoleculeChanged` 中调用 `processor.setMolecularState()` 保存；宿主保存工程时 `getStateInformation()` 转成 XML 写出，加载时 `setStateInformation()` 反序列化回 `molecularState`。
- 编辑器构造时读取 `processor.getMolecularState()`，若有保存状态则 `canvas.restoreMolecule()` 恢复分子，恢复过程再次触发 `onMoleculeChanged` → `applyMoleculeToAudio()` 重新派生 Bell 参数。
- 线程安全：`molecularState` 由 `juce::CriticalSection` 保护。

### 9.7 波形预览

- 右上角波形预览显示 osc_1 的分子波表（`getOsc1WaveData()` → `moleculeWave`），随分子编辑更新。
- 线程安全：音频线程写 `moleculeWave`，UI 线程只读。

### 9.8 历史：旧 wavetable 方案（已旁路）

v1.0.0 之前，合成器是一套「21 化学描述符 → 70 合成参数」的 wavetable 引擎（MorphFilter、
梳状共振体、元音共振峰、LFO1/2、每音随机、慢漂移、glide 等）。这套 DSP 代码仍保留在
`PluginProcessor.h/.cpp`（`MorphFilter`、`FormantFilter`、`combLine*`、`lowCutFilter`、
`highCutFilter`、`paramSmoothers`、`waveTables`、`LoudnessCalibrationThread` 等），
但 `processBlock` 已改为直接 `processBellBlock` 并 `return`，旧 DSP 不再执行。
若要回退，把 `processBlock` 里那两行去掉即可（版本历史 v0.14 记录了旧的映射机制）。

---

## 10. 修改指引（给后续开发者 / AI）

| 我想改… | 去哪里 |
| --- | --- |
| 加新元素（如卤素 F/Cl） | `MoleculeModel.h` 的 `Element` 枚举 + `elementInfo()` 表 + `selectableElements()`（注意新增的 `valenceElectrons` / `electronegativity` 两列必须填） |
| 改元素颜色 / 半径 / 价键 | `elementInfo()` 的 `table[]` |
| **键角太软 / 太硬** | `MoleculeModel.h` 的 `kAngleHeavyK`（骨架，默认 150）与 `kAngleHydrogenK`（氢，默认 55） |
| **锯齿不明显 / 链蜷曲** | 提高 `kAngleHeavyK`，或提高 `kRepulsionK`（1-4 斥力是反式构象的来源） |
| **改理想键角（按元素）** | `MoleculeModel.h` 的 `experimentalBondAngleDegrees()` 表；`Molecule::idealBondAngle()` 只负责查立体数再转发 |
| **环画得太松 / 太紧** | `MoleculeModel.h` 的 `kRingTemplateK`（默认 0.10）。调 0 就退回纯键角求解（会重现凹角问题） |
| **环还是被别出凹角** | 先查 `Molecule::ringAngleTarget()` 是否命中；稠环共享原子在多个环里，只有「同环内前驱/后继」这一对才会用多边形内角 |
| **恢复工程后构型不对** | `Molecule::seedLayout()`（环模板 + BFS 展开），不是 `relaxStep()` 的问题 |
| **加分子预设** | `MoleculeModel.cpp` 的 `moleculePresets()` 表，写元素数组 + 键三元组；芳香环写 Kekulé，逐原子核对价键不超限 |
| **只加常用名不进预设** | `namedMolecules()` 的 `extras` 段，同样写拓扑。**不要手写 SMILES**（见 §7.10） |
| **改预设面板样式** | `PresetMenu.cpp` 顶部匿名 namespace 的尺寸与颜色常量 |
| **调某个分子 → 音色** | 改 `BellEngine.cpp::mapMoleculeToBellParams()` 对应那一行（见 §1.5） |
| **调 Bell 默认音色** | `BellEngine.h` 的 `bellReflectionsPatch()` / `bellParamDef()` 表（默认值与范围） |
| **调 osc_1 波形** | `BellEngine.cpp` 的 `buildNearSineWave()`（谐波幅度与总能量约束） |
| 改键长 / 布局手感 | `MoleculeModel.h` 底部的 `kHeavyBondLength` / `kSpringK` / `kRepulsionK` / `kDamping` |
| **加新 Bell 参数** | `BellEngine.h` 的 `BellParamId` 枚举 + `bellParamDef()` 表 + `applyBellParams()` 映射（测试面板会自动显示，无需改 UI） |
| **加新化学描述符** | `MoleculeModel.h` 的 `ChemicalDescriptors` + `computeDescriptors()`，并在 §7.7 补文献出处 |
| 改动画速度 | 各 `advanceAnimation()` 里的 `dt * 系数`（见 §4.2 表） |
| 改配色 / 视觉风格 | `MoleculeCanvas.cpp` 顶部匿名 namespace 的颜色常量 |
| 改布局尺寸 | `PluginEditor.cpp` 顶部的 `kTopBarHeight` / `kBottomBarHeight` 等常量 |
| 改缩放范围 | `PluginEditor` 构造函数的 `constrainer.setSizeLimits()` |
| 改 auto-fit 松紧 | `MoleculeCanvas.cpp` 的 `kFitPadding`（当前 24 px）与 `viewScaleTarget` 的 clamp 下限 |
| 改结构式格式 | `Molecule::structuralFormula()`（无环）+ `ringStructuralFormula()`（含环） |
| 改/加常见物质常用名 | `MoleculeModel.cpp` 的 `kCommonSubstances[]` 表 |
| 改常用名显示样式 | `MoleculeCanvas::paintStructuralFormula()` 的 `COMMON NAME` 分支 |
| 调音色映射规则 | `BellEngine.cpp` 的 `mapMoleculeToBellParams()`（见 §9.2） |
| 改效果链结构 | `PluginProcessor.cpp` 的 `processBellBlock()` + 成员声明 |

---

## 11. 版本历史

| 版本 | 内容 |
| --- | --- |
| **1.0.1** | **bug 修复：修复高音高（E9 及以上）触发 NaN 崩溃。** 根因是滤波 keytrack 把截止频率推到 Nyquist 以上，TPT 滤波器 tan() 溢出为 Inf/NaN 并永久污染状态，导致电流声后整机静音。修复：钳制截止频率到 [20, 0.45×sr]，输出端加 NaN 兜底并重置滤波器；BellTests 新增高音高回归测试 |
| **1.0.0** | **第一个正式版本。** 版本号从 0.9.0 升到 1.0.0；暂时屏蔽 Test 调试面板按钮；含自动更新检查 + 更新弹窗、每日匿名遥测、README/LICENSE/FUNDING/安装器脚本，Bell 音色复刻作为起点音色 |
| 0.1.0 | 纯白界面 + 左上角官网链接 |
| 0.2.0 | 分子编辑器交互框架：元素栏、画布、左键放原子 / 右键删原子、自动补氢、力导向布局、顶栏分子式 |
| 0.3.0 | 全英文化；等比缩放；分子增大时 auto-fit；删除后保留最大片段；点击键切换单/双/三键；左下角凝聚式结构式 |
| 0.4.0 | 拖拽放置原子 + 拖拽成键；右键断裂化学键；环状结构式（SMILES）；常见物质识别与英文常用名 |
| 0.5.0 | 移除圆形培养皿边界；向心力改为质心整体回中（分子自由舒展）；auto-fit 改为矩形画布空间适配 |
| 0.6.0 | 分子→声音映射层：化学描述符 + 正弦波声源 + 低切/混响/延迟效果链，随分子拓扑实时变化 |
| 0.7.0 | 放开波形限制：SMILES→wavetable 任意波形（无锁双缓冲发布）；映射规则范围大幅拉宽，音色变化更夸张 |
| 0.7.1 | 末端加 tanh 软削波消除爆音；分子拓扑变化时弱 fade-out/fade-in 消除电流声 |
| 0.7.2 | 修复 delay 第一声电流声：delay 时间逐样本平滑并保留小数（fractional delay），最大缓冲提至 1.5s；网址右侧加版本号标识 |
| 0.7.3 | 修复添加/删除原子时的短促电流声（voice 检测波形版本变化，切换瞬间重置包络）；实现 DAW 工程持久化（分子拓扑序列化为 ValueTree，关闭界面重开可恢复） |
| 0.7.4 | 彻底消除添加原子残留电流声：波形切换的 fade 触发时同步清空 delay 线 / reverb 内部状态，清除旧波形尾音 |
| 0.8.0 | 分子绘制区右上角新增波形预览窗口（180×50）：无音频输出时显示分子 wavetable 波形，有输出时显示输出信号波形 |
| 0.8.1 | 波形预览窗口改为只显示静态的当前分子 wavetable 波形，移除输出信号实时波形与相关快照/电平逻辑 |
| 0.9.0 | 顶部新增 Test 按钮展开合成器参数调试面板：8 个效果参数全部暴露，分子变化自动跟随映射刷新，支持手动覆盖任意参数并一键 Reset to auto |
| 0.10.0 | 合成器参数系统重构为数组化 + 分类元数据；参数扩展到 22 个（振荡器/包络/滤波/失真/合唱/混响/延迟/主输出），新增高切(低通)、失真、合唱、立体声宽度处理流程；测试面板改为可滚动 + 按处理类型分类 |
| 0.10.1 | 修复测试面板 chorusmix/delaymix 被下一分类标题覆盖无法调整的布局问题；Reset to auto 时未被分子映射的参数（HighCutRes/ReverbWidth/MasterLevel）回归元数据默认值 |
| 0.11.0 | **① VSEPR 键角力场**：relaxStep 加入角弯曲项（θ₀ 由立体数决定 109.5/120/180°），范德华斥力排除 1-2/1-3 对，共线退化处理，addAtom 按理想角落子——烷烃链终于是锯齿而非直线。**② 化学描述符 10 → 21 个**：SSSR 环感知、Hückel 4n+2 芳香性、TPSA(Ertl)、cLogP(Crippen)、可旋转键(Veber)、氢键供受体(Lipinski)、Fsp3、Wiener 指数、Randić 指数、键极性。**③ 合成参数 22 → 37 个**：形态滤波器（LP↔BP↔HP↔Notch 连续变形 + 频率 + 谐振 + 包络跟随）、子振荡器、相位畸变 warp、声像展开、完整 ADSR、四种失真曲线、全局 LFO（速率 + 三路调制目标）、乒乓延迟。**④ 映射全面重写**：每条映射均有化学依据（芳香环电流→乒乓延迟、TPSA→滤波器形态、cLogP→滤波频率、氢键供体→sustain 等）。**⑤ 波形生成**加入芳香性（奇次谐波）/ 键极性（偶次谐波）/ Fsp3（相位规整度）塑形 |
| 0.12.0 | **① 环几何修复**：环内键角目标改为多边形内角 `180°−360°/n`（环己烷 120°、环丙烷 60°），并新增环模板力把环成员约束到正多边形外接圆——消除成环时的凹角多边形，也消除了 `acos()` 无符号夹角带来的镜像简并。**② 恢复布局修复**：`fromValueTree()` 新增 `seedLayout()`，先摆环模板（稠环按共享边镜像）再 BFS 长链，取代原来的"全部原子铺一个圈"——关闭界面再打开不会再把苯环还原成心形。**③ sp³ 键角按元素取实验值**：C 112.4°/N 110.9°/O 111.7°/S 99.1°/P 98.6°（两侧含氢时取氢化物值 109.5/107.8/104.5/92.1/93.5°），来源 CRC Handbook 与 Allen 1987。**④ 修复参数平滑滞后**：块速率消费的平滑器每块只前进 1 步，100 ms 斜坡被拉成约 51 秒，改用 `skip(numSamples)`；顺带修掉 `LfoRate` 被重复消费导致的双倍推进。**⑤ 顶栏分子预设**：33 种常见有机分子，点击分子式下拉选择，`<` `>` 循环切换；预设以拓扑直写（非 SMILES）复用 ValueTree 通路。**⑥ 环感知结果缓存**：`rings()` 按 `topologyVersion` 记忆化，避免 60 fps 求解器每帧重跑 SSSR |
| **0.14.0** | **映射退回 + 渐进接线机制。** v0.13 一次性把 70 个参数全接上分子导致音色变差，本版把映射退回 v0.12 的 37 条，新增的 33 个参数全部旁通（DSP 保留、随时可接）。**① `ParamDef` 新增 `mapped` 标记**，成为「哪些参数由分子驱动」的唯一事实来源，面板与映射层共用，不会各自漂移。**② 修正 4 个非中性默认值**——这是真正让 v0.13 变声的原因，而不是架构：`Velocity sens` 0.40 → **1.0**（0 是旁通的反面：忽略力度、每音满音量）、`Env curve` 0.45 → **0**（>0 无条件对包络求幂，压低音量并加快衰减）、`Filter key trk` 0.35 → **0**、`Filter vel trk` 0.20 → **0**。**③ 澄清 per-voice 滤波器的等价性**：SVF 线性且 `filter drive = 0` 时路径无非线性，只要 key/vel trk 与各调制深度为 0，所有 voice 共享同一截止频率，`Σ filter(voiceᵢ) ≡ filter(Σ voiceᵢ)`，与 v0.12 单个全局滤波器**数学等价**——所以架构改动不必回退，它是 `key trk` 的实现前提。**④ `Filter env amt` 系数 2.6 → 2.1**，对齐 v0.12 `envAmt × 2.5 oct` 在满值时的 2.125 oct 扫动幅度（驱动源仍为 per-voice ADSR，比原包络跟随器更干净、无跨音符串扰）。**⑤ Test 面板三态配色**：深灰 = 分子驱动、浅灰 + `[free]` = 待接线、蓝 = 手动锁定。**⑥ 共振体加 `mix > 0.001` 守卫**（原本声学上已旁通，此改动只为省掉逐样本延迟线读写）。**⑦ 待接线参数的映射公式全部作为注释保留在原位**，接线时直接启用 |
| **0.13.0** | **① 自绘预设面板**：新增 `PresetMenu` 组件取代 `juce::PopupMenu`——全屏遮罩 + 3 列卡片 + 逐列填充 + 右侧分子式 + 左侧强调竖条，风格完全自控；抬头去掉倒三角，改为悬停细下划线。**② 空分子静音**：`setMoleculeEmpty()` + 12 ms 平滑门 + `allNotesOff`，画布无原子时彻底安静且不留混响尾巴。**③ 常用名库重写**：名字索引改为运行时由拓扑反算 canonical SMILES 自动建立（手写 SMILES 字符串表会静默腐烂，原表里 `"CC=O(O)"` 就是永远匹配不上的死条目），收录扩到 78 种。**④ 合成器现代化重构**：形态滤波器从全局链**下沉到每个 voice**，由此支持 key tracking / velocity tracking / filter drive；新增调制包络 ENV2（→ 滤波双向 / 音高 / warp）、LFO1 四种波形连续插值 + 淡入 + → 音高、每 voice LFO2、每音随机源（音高 / 滤波 / 声像）、每 voice 慢漂移（相位与速率各带 ±25% 抖动）、噪声层、glide、包络曲线、力度灵敏度、跟随音高的梳状共振体（Karplus-Strong + 阻尼）、元音共振峰（Peterson-Barney F1/F2/F3，A/E/I/O/U 插值）。参数 37 → **70**，分类 9 → 14。**⑤ 映射全面扩写**并**前置为 §1.5 调试速查总表**（中间归一量 + 逐参数公式 + 听感 + "想听 X 就改 Y"对照）。新映射亮点：环张力 → ENV2 音高弯折、简谐振子 ω∝√(k/m) → LFO2 速率、Graham 扩散定律 → 漂移速率、F=ma → 力度灵敏度、环尺寸 → 共振体音程、氢键供体 → 共振峰混合量 |

---

## 12. 本次开发记录（v1.0.1）

> 本轮定位并修复了一个严重崩溃：高音高 MIDI 输入会让合成器永久失效。

### 12.1 症状
- 向当前音源发送很高音高的 MIDI 信号（如甲烷 CH4 时输入 E9，即 MIDI note 124），会先出现一声电流声，随后**整个合成器不再响应任何 MIDI 输入**，像"爆掉了"。
- 不同分子触发崩溃的音高不同，说明触发阈值与分子的音色映射参数（滤波截止 / keytrack）有关。

### 12.2 根因
音频路径为 Bell 模式（`PluginProcessor::processBlock` → `processBellBlock`，旧分子模式 DSP 已 `return` 旁路）。崩溃点在 `BellVoice::updateFilter`：

```cpp
cutoffHz = filterCutoff × 2^(keytrackCents/12) × 2^(env2ToFilter·env2·4/12)
```

高音高下 keytrack 把低通滤波器的截止频率推到远超 Nyquist（24000 Hz，E9 时可达 10⁵ Hz 量级）。`juce::dsp::StateVariableTPTFilter::setCutoffFrequency` 内部计算 `g = tan(π·fc/sr)`，在 fc 超过 Nyquist 后 `tan()` 溢出为 Inf/NaN。TPT 是递归反馈结构，状态一旦被 NaN 污染就**永久保持 NaN**（即使该音符释放、voice 被复用，状态仍是 NaN），于是所有后续输出变 NaN → 静音。

### 12.3 修复
1. **钳制截止频率**（`BellEngine.cpp::updateFilter`）：`cutoffHz = jlimit(20, sampleRate×0.45, cutoffHz)`，从根上阻止 tan 溢出（0.45×sr 低于 Nyquist，留有安全余量）。
2. **NaN 兜底**（`BellEngine.cpp::renderNextBlock`）：每个输出采样前 `std::isfinite` 检测，非有限值则输出 0 并 `reset()` 两个滤波器，避免单次溢出永久污染。
3. **回归测试**（`BellTests.cpp`）：新增 E9/G9（note 124/127）高音渲染 + 高音后普通音恢复检测。

### 12.4 涉及文件
| 文件 | 改动 |
| --- | --- |
| `BellEngine.cpp` | `updateFilter` 截止频率钳制；`renderNextBlock` 输出 NaN 兜底 + 滤波器重置 |
| `BellTests.cpp` | 新增高音高 NaN 检测与整机恢复回归测试 |

---

*文档维护：本文档应与代码同步更新。若修改了 §7 涉及的任何化学规则，请同时更新该节的对照表与简化说明。*

<h1 align="center">Organic Chemistry</h1>

<p align="center"><strong>分子即音色 · 用化学键合成声音</strong></p>

<p align="center">
  <em>Molecule as preset — build a molecule, hear its sound.</em>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-0.9.0-2A2A28" alt="Version">
  <img src="https://img.shields.io/badge/platform-Windows%20%7C%20macOS-lightgrey" alt="Platform">
  <img src="https://img.shields.io/badge/framework-JUCE%208.0.12-orange" alt="JUCE">
  <img src="https://img.shields.io/badge/license-GPL--3.0-blue" alt="License">
</p>

<p align="center">
  <code>C · O · N · S · P → 波形 · 滤波 · 共振 · 空间</code><br>
  <code>21 个化学描述符 → 70 个合成参数</code>
</p>

---

## 概述 / Overview

**Organic Chemistry** 是一款把有机化学的成键规则编码进 UI 的合成器。你在界面里"搭建分子"，分子的化学性质实时映射为音色参数——**分子即预设**。

> **中文**：一款以分子结构编辑器作为主交互界面的 wavetable 合成器。用户拖拽原子、成键、切换键级，分子的拓扑、不饱和度、芳香性、极性等 21 项化学描述符被实时映射为波形与效果链参数，让每一次分子编辑都带来可归因的音色变化。
>
> **English**: A wavetable synthesiser whose primary interface is a molecular structure editor. You place atoms, form bonds and cycle bond orders; the molecule's topology, unsaturation, aromaticity and polarity are mapped in real time to the waveform and effect chain — every edit produces an attributable timbre change.

> 分类 Category：Synth / Instrument ｜ 插件代码 Plug-in Code：`OrCh` ｜ 厂商 Vendor：iisaacbeats.cn

---

## 功能模块 / Modules

| 模块 / Module | 功能描述 / Description |
|------|---------|
| **分子编辑器 Molecule editor** | C / O / N / S / P 五种元素，拖拽放置原子、拖拽成键、右键删除 / 断键、点击键循环单 / 双 / 三键，受价键约束，自动补齐氢原子。<br>*5 elements, drag-to-place, drag-to-bond, right-click delete / break bonds, click to cycle single/double/triple, valence-checked with implicit hydrogens.* |
| **几何力场 Geometry** | VSEPR 键角力场：sp / sp² / sp³ 键角、按元素取实验值（C–S–C 99° 等）、环内多边形内角 + 环模板，分子呈现正确的锯齿与正多边形构象。<br>*VSEPR bond-angle force field with element-specific experimental angles and ring templates.* |
| **分子识别 Recognition** | Hill 分子式、分子量、凝聚式 / SMILES 结构式、78 种常见物质英文常用名（Benzene、Water、Caffeine …）。<br>*Hill formula, molecular weight, condensed/SMILES structure, 78 common-name recognitions.* |
| **分子预设 Presets** | 33 种常见有机分子，自绘 3 列卡片面板，`<` `>` 循环切换。<br>*33 common molecules in a self-drawn 3-column card picker.* |
| **分子 → 声音映射 Mapping** | 21 个化学描述符 → 70 个合成参数，每条映射都有化学依据（芳香环电流 → 乒乓延迟、F=ma → 力度灵敏度、环张力 → 音头弯音等）。<br>*21 chemical descriptors mapped to 70 synth parameters, each with a chemical rationale.* |
| **合成引擎 Synth engine** | 8 复音 wavetable、每 voice 形态滤波器（key / vel 跟踪 + drive）、梳状共振体、元音共振峰、失真、合唱、混响、乒乓延迟，完整调制系统（ADSR、ENV2、LFO1/LFO2、每音随机、慢漂移）。<br>*8-voice wavetable, per-voice morph filter, comb resonator, formant, distortion, chorus, reverb, ping-pong delay, full modulation matrix.* |
| **Bell 音色 Bell timbre** | 内置参考音色复刻（三正弦振荡器 Bell voice），可作为开发的起点音色。<br>*Built-in reference bell timbre (three-sine Bell voice) as a starting point.* |

---

## 技术栈 / Tech Stack

| 项目 / Item | 版本 / Version |
|------|------|
| 语言 Language | C++17 |
| 框架 Framework | [JUCE](https://juce.com) 8.0.12（FetchContent 自动拉取） |
| 构建 Build | CMake ≥ 3.22 |
| 化学计算 Chemistry | 全部自研（无外部化学库依赖）：VSEPR 键角、SSSR 环感知、Hückel 芳香性、21 个分子描述符 |

---

## 构建 / Build

```bash
# 克隆仓库 Clone
git clone https://github.com/sweetorange1/Organic-Chemistry.git
cd Organic-Chemistry

# CMake 配置 & 构建（Release）
cmake -B cmake-build-release -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release --config Release
```

构建成功后 VST3 会自动复制到 `%LOCALAPPDATA%\Programs\Common\VST3`（Windows，无需管理员权限）。
*On success the VST3 is copied to `%LOCALAPPDATA%\Programs\Common\VST3` (Windows, no admin rights needed).*

## 打包安装器 / Packaging

```bash
# Windows：需要先安装 Inno Setup 6
build_installer.bat
# 产物 Output：dist\OrganicChemistry_Setup_0.9.0_x64.exe

# macOS：打包通用二进制（x86_64 + arm64）的 pkg / dmg
./build_installer_mac.sh
# 产物 Output：dist\OrganicChemistry_Setup_0.9.0_macOS.pkg / .dmg
```

---

## 隐私说明 / Privacy

- **更新检查 Update check**：启动后异步请求 `iisaacbeats.cn` 一次（5s 超时，失败静默），仅在有新版本时弹窗提示。
  *Async check to `iisaacbeats.cn` on startup (5s timeout, silent on failure); prompts only when a new version exists.*
- **匿名遥测 Telemetry**：每日一次匿名的"界面打开"事件（无任何音频数据、无个人身份信息），客户端为随机 UUID。
  *Anonymous "ui opened" event once per day (no audio, no PII), random client UUID.*
- 停用遥测 Disable telemetry：启动前设置环境变量 `IISAAC_TELEMETRY_DISABLED=1`。
  *Set `IISAAC_TELEMETRY_DISABLED=1` before launch.*

---

## 许可 / License

本项目基于 [JUCE](https://juce.com) 框架，按 **GNU General Public License v3.0** 发布（详见 [LICENSE](LICENSE)）。JUCE 8 采用 GPL-3.0 / 商业双授权，本仓库以 GPL-3.0 条款分发。
*Released under the GNU GPL v3.0 (see [LICENSE](LICENSE)). JUCE 8 is dual-licensed (GPL-3.0 / commercial); this repository is distributed under GPL-3.0.*

| 组件 / Component | 许可 / License |
|------|------|
| JUCE 8 | GPL-3.0 / 商业双授权 GPL-3.0 / commercial dual |

---

<p align="center">
  <code>C1=CC=CC=C1 → 中空的奇次谐波 · 乒乓回声</code><br>
  <em>搭一个苯环，听它的环电流。</em><br>
  <em>Build a benzene ring and hear its ring current.</em><br><br>
  &copy; 2024-2026 iisaacbeats.cn
</p>

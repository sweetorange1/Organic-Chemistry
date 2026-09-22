#!/usr/bin/env bash
# =============================================================================
# Organic Chemistry — macOS 安装包打包脚本
# -----------------------------------------------------------------------------
# 用途：把以 Release 构建产出的 VST3 + AU 组件，打包成一个 macOS 标准安装器
#       (.pkg)，可选再封装成 .dmg 便于分发。
#
# 设计原则（对齐 Windows 端 build_installer.bat）：
#   1) 本脚本不触碰编译。编译由 CLion / cmake 负责。
#   2) 本脚本只做：
#        a. 校验 Release 版 VST3 + AU 产物是否存在
#        b. 校验产物为 Universal 二进制（同时含 arm64 与 x86_64）
#        c. （可选）codesign 签名（传入 --sign "Developer ID Application: ..."）
#        d. 用 pkgbuild 分别打两个组件 pkg
#        e. 用 productbuild 合成一个用户可见的安装器 .pkg
#        f. （可选）用 hdiutil 生成 .dmg 磁盘映像
#   3) 版本号从 CMakeLists.txt 的 project(VERSION ...) 自动读取，
#      与 Windows iss 的 MyAppVersion 语义保持一致。
#
# 前置条件：
#   * 已在 CLion 中以 Release 配置构建 OrganicChemistry 目标（会同时产出 VST3 与 AU）
#   * CMake 已配置双架构（仓库 CMakeLists.txt 已默认设置 CMAKE_OSX_ARCHITECTURES），
#     若存在旧的 cmake-build-release 缓存，需删掉该目录重新 configure 才会生效
#   * 产物目录：cmake-build-release/OrganicChemistry_artefacts/Release/{VST3,AU}
#   * macOS 自带 pkgbuild / productbuild / hdiutil / lipo，无需额外安装
#
# 输出目录：dist/
#   * OrganicChemistry_Setup_<ver>_macOS.pkg     ← 双击即可安装
#   * OrganicChemistry_Setup_<ver>_macOS.dmg     ← 便于分发（默认生成）
#
# 用法：
#   chmod +x build_installer_mac.sh
#   ./build_installer_mac.sh                              # 打 pkg + dmg（不签名）
#   ./build_installer_mac.sh --no-dmg                     # 只打 pkg（不签名）
#   ./build_installer_mac.sh --sign "Developer ID Application: ..."   # 签名后打包
# =============================================================================

set -euo pipefail

# ---------- 路径与常量 ----------
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BUILD_DIR="${SCRIPT_DIR}/cmake-build-release/OrganicChemistry_artefacts/Release"
VST3_SRC="${BUILD_DIR}/VST3/Organic Chemistry.vst3"
AU_SRC="${BUILD_DIR}/AU/Organic Chemistry.component"

DIST_DIR="${SCRIPT_DIR}/dist"
STAGING_DIR="${SCRIPT_DIR}/.pkg_staging"

APP_NAME="Organic Chemistry"
APP_PUBLISHER="iisaacbeats.cn"
# 唯一标识符（与 Windows 端 AppId 语义对应，macOS 用反向域名）
VST3_PKG_ID="cn.iisaacbeats.organicchemistry.vst3"
AU_PKG_ID="cn.iisaacbeats.organicchemistry.au"
PRODUCT_PKG_ID="cn.iisaacbeats.organicchemistry.installer"

MAKE_DMG=1
SIGN_IDENTITY=""
for arg in "$@"; do
  case "$arg" in
    --no-dmg) MAKE_DMG=0 ;;
    --sign) SIGN_IDENTITY="__NEXT__" ;;
    -h|--help)
      echo "用法: $0 [--no-dmg] [--sign \"Developer ID Application: ...\"]"
      exit 0
      ;;
    *)
      if [[ "$SIGN_IDENTITY" == "__NEXT__" ]]; then
        SIGN_IDENTITY="$arg"
      else
        echo "[WARN] 未知参数: $arg（已忽略）"
      fi
      ;;
  esac
done
if [[ "$SIGN_IDENTITY" == "__NEXT__" ]]; then
  SIGN_IDENTITY=""
fi

# ---------- 1) 从 CMakeLists.txt 抽取版本号 ----------
CMAKE_FILE="${SCRIPT_DIR}/CMakeLists.txt"
if [[ ! -f "$CMAKE_FILE" ]]; then
  echo "[ERROR] 未找到 CMakeLists.txt: $CMAKE_FILE"
  exit 1
fi
# 匹配形如：project(OrganicChemistry VERSION 0.9.0 LANGUAGES C CXX)
RAW_VER="$(grep -Eo 'project\([^)]*VERSION[[:space:]]+[0-9]+\.[0-9]+\.[0-9]+' "$CMAKE_FILE" | grep -Eo '[0-9]+\.[0-9]+\.[0-9]+' | head -n1 || true)"
if [[ -z "$RAW_VER" ]]; then
  echo "[ERROR] 无法从 CMakeLists.txt 提取版本号（project(VERSION ...) 未匹配到 X.Y.Z 格式）"
  exit 1
fi
APP_VERSION="$RAW_VER"
echo "[INFO] 检测到版本号: ${APP_VERSION}"

# ---------- 2) 校验 Release 产物 ----------
if [[ ! -d "$VST3_SRC" ]]; then
  echo "[ERROR] 未找到 Release 版 VST3 产物: $VST3_SRC"
  echo "        请先在 CLion 中以 Release 配置构建 OrganicChemistry 目标后再运行本脚本。"
  exit 1
fi
if [[ ! -d "$AU_SRC" ]]; then
  echo "[ERROR] 未找到 Release 版 AU 产物: $AU_SRC"
  echo "        请先在 CLion 中以 Release 配置构建 OrganicChemistry 目标后再运行本脚本。"
  exit 1
fi
echo "[INFO] VST3 源: $VST3_SRC"
echo "[INFO] AU   源: $AU_SRC"

# ---------- 2.5) 校验产物是 arm64 + x86_64 通用二进制 ----------
# 只打通用包：单架构产物必须在这里拦下，避免误分发。
check_universal() {
  local label="$1"
  local bundle="$2"
  local bin="$bundle/Contents/MacOS/${APP_NAME}"

  if [[ ! -f "$bin" ]]; then
    echo "[ERROR] 在 ${bundle} 中未找到可执行文件 Contents/MacOS/${APP_NAME}"
    exit 1
  fi

  local archs
  archs="$(lipo -archs "$bin" 2>/dev/null || true)"
  if [[ -z "$archs" ]]; then
    echo "[ERROR] 无法用 lipo 读取 ${label} 的架构信息: $bin"
    exit 1
  fi

  echo "[INFO] ${label} 架构: ${archs}"
  if [[ "$archs" != *arm64* || "$archs" != *x86_64* ]]; then
    echo "[ERROR] ${label} 不是 Universal 二进制（需同时包含 arm64 与 x86_64，当前: ${archs}）"
    echo "        请删除构建缓存后重新 configure + 完整重编："
    echo "        rm -rf cmake-build-release"
    echo "        cmake -B cmake-build-release -DCMAKE_BUILD_TYPE=Release"
    echo "        cmake --build cmake-build-release --config Release"
    exit 1
  fi
}

check_universal "VST3" "$VST3_SRC"
check_universal "AU"   "$AU_SRC"

# ---------- 3) 准备目录 ----------
rm -rf "$STAGING_DIR"
mkdir -p "$STAGING_DIR/vst3_root/Library/Audio/Plug-Ins/VST3"
mkdir -p "$STAGING_DIR/au_root/Library/Audio/Plug-Ins/Components"
mkdir -p "$STAGING_DIR/components"
mkdir -p "$STAGING_DIR/resources"
mkdir -p "$DIST_DIR"

# 拷贝产物到 staging 树中的目标绝对路径
echo "[INFO] 拷贝插件产物到 staging..."
cp -R "$VST3_SRC" "$STAGING_DIR/vst3_root/Library/Audio/Plug-Ins/VST3/"
cp -R "$AU_SRC"   "$STAGING_DIR/au_root/Library/Audio/Plug-Ins/Components/"

# ---------- 3.5) 可选：codesign 签名 ----------
if [[ -n "$SIGN_IDENTITY" ]]; then
  echo "[INFO] 使用身份签名: $SIGN_IDENTITY"
  codesign --force --options runtime --timestamp \
    --sign "$SIGN_IDENTITY" \
    "$STAGING_DIR/vst3_root/Library/Audio/Plug-Ins/VST3/${APP_NAME}.vst3"
  codesign --force --options runtime --timestamp \
    --sign "$SIGN_IDENTITY" \
    "$STAGING_DIR/au_root/Library/Audio/Plug-Ins/Components/${APP_NAME}.component"
else
  echo "[INFO] 未指定签名身份，跳过 codesign（产物为未签名版本，分发时建议签名）"
fi

# ---------- 4) 分别打两个组件 pkg ----------
VST3_COMPONENT_PKG="$STAGING_DIR/components/${APP_NAME}-VST3.pkg"
AU_COMPONENT_PKG="$STAGING_DIR/components/${APP_NAME}-AU.pkg"

echo "[INFO] pkgbuild → VST3 组件..."
pkgbuild \
  --root "$STAGING_DIR/vst3_root" \
  --identifier "$VST3_PKG_ID" \
  --version "$APP_VERSION" \
  --install-location "/" \
  "$VST3_COMPONENT_PKG"

echo "[INFO] pkgbuild → AU 组件..."
pkgbuild \
  --root "$STAGING_DIR/au_root" \
  --identifier "$AU_PKG_ID" \
  --version "$APP_VERSION" \
  --install-location "/" \
  "$AU_COMPONENT_PKG"

# ---------- 5) 生成 Distribution.xml（内联） ----------
DIST_XML="$STAGING_DIR/Distribution.xml"
cat > "$DIST_XML" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>${APP_NAME} ${APP_VERSION}</title>
    <organization>${APP_PUBLISHER}</organization>
    <domains enable_localSystem="true"/>
    <options customize="allow" require-scripts="false" rootVolumeOnly="true" hostArchitectures="x86_64,arm64"/>
    <welcome file="welcome.txt" mime-type="text/plain"/>
    <choices-outline>
        <line choice="default">
            <line choice="vst3"/>
            <line choice="au"/>
        </line>
    </choices-outline>
    <choice id="default" title="${APP_NAME}" description="${APP_NAME} ${APP_VERSION} — 分子编辑器 wavetable 合成器"/>
    <choice id="vst3" visible="true" title="VST3 插件" description="安装到 /Library/Audio/Plug-Ins/VST3/${APP_NAME}.vst3（所有用户共用）">
        <pkg-ref id="${VST3_PKG_ID}"/>
    </choice>
    <choice id="au" visible="true" title="Audio Unit (AU) 插件" description="安装到 /Library/Audio/Plug-Ins/Components/${APP_NAME}.component（所有用户共用）">
        <pkg-ref id="${AU_PKG_ID}"/>
    </choice>
    <pkg-ref id="${VST3_PKG_ID}" version="${APP_VERSION}" onConclusion="none">${APP_NAME}-VST3.pkg</pkg-ref>
    <pkg-ref id="${AU_PKG_ID}" version="${APP_VERSION}" onConclusion="none">${APP_NAME}-AU.pkg</pkg-ref>
</installer-gui-script>
XML

# 欢迎页文案
cat > "$STAGING_DIR/resources/welcome.txt" <<TXT
${APP_NAME} ${APP_VERSION}

一款以分子结构编辑器作为主交互界面的 wavetable 合成器，
分子即预设——搭一个分子，听它的音色。

通用二进制（Universal）：同时支持 Apple Silicon (arm64) 与 Intel (x86_64) Mac。

本安装器将为您安装两种格式的插件（可在下一步自定义勾选）：

  • VST3  → /Library/Audio/Plug-Ins/VST3/${APP_NAME}.vst3
  • AU    → /Library/Audio/Plug-Ins/Components/${APP_NAME}.component

安装完成后，请在您的 DAW 中重新扫描插件目录。

发布方：${APP_PUBLISHER}
TXT

# ---------- 6) productbuild 合成最终安装器 ----------
FINAL_PKG="$DIST_DIR/OrganicChemistry_Setup_${APP_VERSION}_macOS.pkg"
echo "[INFO] productbuild → 最终安装器..."
productbuild \
  --distribution "$DIST_XML" \
  --package-path "$STAGING_DIR/components" \
  --resources "$STAGING_DIR/resources" \
  --identifier "$PRODUCT_PKG_ID" \
  --version "$APP_VERSION" \
  "$FINAL_PKG"

echo "[OK] 已生成安装包: $FINAL_PKG"

# ---------- 6.5) 可选：对最终 pkg 也做签名 ----------
if [[ -n "$SIGN_IDENTITY" ]]; then
  SIGNED_PKG="$DIST_DIR/OrganicChemistry_Setup_${APP_VERSION}_macOS_signed.pkg"
  echo "[INFO] productsign → 签名安装器..."
  productsign --sign "$SIGN_IDENTITY" "$FINAL_PKG" "$SIGNED_PKG"
  mv -f "$SIGNED_PKG" "$FINAL_PKG"
  echo "[OK] 安装器已签名: $FINAL_PKG"
fi

# ---------- 7) 可选：封装成 dmg ----------
if [[ "$MAKE_DMG" == "1" ]]; then
  FINAL_DMG="$DIST_DIR/OrganicChemistry_Setup_${APP_VERSION}_macOS.dmg"
  DMG_STAGING="$STAGING_DIR/dmg"
  rm -rf "$DMG_STAGING"
  mkdir -p "$DMG_STAGING"
  cp "$FINAL_PKG" "$DMG_STAGING/"

  cat > "$DMG_STAGING/README.txt" <<TXT
${APP_NAME} ${APP_VERSION} — macOS 安装说明

1. 双击 OrganicChemistry_Setup_${APP_VERSION}_macOS.pkg 启动安装器。
2. 首次运行如遇 Gatekeeper 拦截，请在 系统设置 → 隐私与安全性 中允许运行。
3. 安装完成后在 DAW 中重新扫描 VST3 / AU 插件目录即可看到 ${APP_NAME}。

发布方：${APP_PUBLISHER}
TXT

  rm -f "$FINAL_DMG"

  echo "[INFO] hdiutil → dmg..."
  hdiutil create \
    -volname "${APP_NAME} ${APP_VERSION}" \
    -srcfolder "$DMG_STAGING" \
    -ov \
    -format UDZO \
    "$FINAL_DMG" >/dev/null

  echo "[OK] 已生成磁盘映像: $FINAL_DMG"
fi

# ---------- 8) 清理 staging ----------
rm -rf "$STAGING_DIR"

echo ""
echo "===================================================================="
echo " 打包完成！输出目录: $DIST_DIR"
ls -lh "$DIST_DIR" | awk 'NR>1 {print "   " $9 "   " $5}'
echo "===================================================================="

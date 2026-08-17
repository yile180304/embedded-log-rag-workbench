#pragma once

// RAG 诊断工作台 · 视觉 token 与全局样式表
//
// 审美方向：**薄荷底 · 白色浮层 · 圆角极简**
//
//   窗体底色是一片低饱和薄荷绿，全部内容浮在它上面的一整块白色圆角面板里。
//   层级不靠边框表达，靠"白 vs 薄荷"这一次明确的色彩落差，加上面板下方的柔和投影。
//   面板内部则几乎不再有第二次落差——卡片是白底发丝边，表格干脆没有容器。
//
// 三条构成规则（这套语言的全部）：
//   1. 圆角给足：控件 12、容器 18、色块 20、按钮与标签做成全圆胶囊。
//      这套语言里没有直角，出现直角会立刻显得廉价。
//   2. 分隔靠留白和淡色填充，不靠线。表格无网格线、卡片只留发丝边、
//      区块之间用 24/32 的间距代替分隔线。
//   3. 强调色只有一个（青绿），并且只有两种用法：实心色块承载主操作，
//      淡薄荷填充表示选中/归属。语义色只在真的发生时出现。
//
// 密度：这是低密度设计。行高 40、控件 38、卡片内边距 24。
// 同屏信息量比紧凑档少约三分之一——这是这个方向的直接代价，不是疏忽。
//
// 技术现实：
//   · QSS 没有 box-shadow。白色浮层下方的柔和投影由 ragui::SurfaceBackground
//     自绘（同心圆角矩形按二次衰减叠加），不是样式表能做的事。
//   · QSS 没有 transition / line-height / text-overflow，写了会被静默丢弃。
//   · 下拉框与数字框的箭头只认 `image: url(...)`。自绘三角（transparent-border 技法）
//     会渲染成实心色块，而只声明按钮区宽度、不给图又会把原生箭头整个抑制掉。
//     所以箭头是启动时画成 PNG 落盘再喂进来的，见 ragui::arrowAsset()。
//
// 这是全项目唯一的样式来源。QSS 单一加载入口是 DiagnosticWorkbench::applyTheme()，
// 其它任何地方都不要调用 setStyleSheet()——控件级内联样式表会把圆角和内边距一起冲掉，
// 导致控件在状态切换时跳变。需要按状态换外观时，用动态属性 + ragui::repolish()。

#include <QString>

namespace ragui {

/// 调色板。
///
/// 对比度取值不是从参考稿里直接吸的色——设计稿的浅灰文字普遍在 3:1 左右，
/// 那是给截图看的，不是给人盯一整天的。这里所有承载信息的文字都拉到了 4.5:1 以上：
/// 正文 9.6:1、次要 5.1:1、青绿实心块上的白字 4.9:1。只有占位符停在 3.6:1
/// （占位符不是内容，这是可接受的例外）。
struct RagPalette
{
    // ---- 底色与浮层 ----
    QString ground       = QStringLiteral("#CFE1DD"); // 薄荷底，窗体的"桌面"
    QString groundDeep   = QStringLiteral("#C4D9D4"); // 底色的暗角，极弱的渐变落点
    QString groundShadow = QStringLiteral("#1D403A"); // 投影颜色：带底色的绿相，不是纯黑
    // 对话框铺一层更淡的薄荷。比主窗浅，因为它是浮在主窗之上的一层；
    // 和主窗同色会让人分不清对话框边界在哪。
    QString dialogGround = QStringLiteral("#EFF6F4");
    QString surface      = QStringLiteral("#FFFFFF"); // 白色浮层
    QString surfaceMuted = QStringLiteral("#F5F8F8"); // 输入框 / 内嵌区
    QString surfaceHover = QStringLiteral("#F1F6F6");
    QString surfaceAlt   = QStringLiteral("#F8FBFB"); // 表格交替行，几乎察觉不到

    // ---- 淡彩填充（参考稿里的分类底色）----
    QString tintMint     = QStringLiteral("#E4F1EE");
    QString tintLilac    = QStringLiteral("#EFECFB");

    // ---- 边与线 ----
    // 只有三档，且都极淡。白面板上的边框存在感一旦超过发丝，整屏就会变回"表单"。
    QString border       = QStringLiteral("#E9EEF0");
    QString borderStrong = QStringLiteral("#D7E1E4");
    QString divider      = QStringLiteral("#EEF2F3");

    // ---- 文字 ----
    QString textStrong   = QStringLiteral("#16283A"); // 15.4:1
    QString text         = QStringLiteral("#33475B"); //  9.6:1
    QString textMuted    = QStringLiteral("#5F7183"); //  5.1:1
    QString textFaint    = QStringLiteral("#8797A5"); //  3.6:1 —— 仅占位符与装饰
    QString textOnAccent = QStringLiteral("#FFFFFF");

    // ---- 强调色（全局只有这一个）----
    // 参考稿的青绿更亮（约 #3E9C92），但白字压在上面只有 3.3:1。
    // 这里压深到 4.9:1，色相不变，肉眼几乎看不出差别，可读性差一个等级。
    QString accent       = QStringLiteral("#237E74");
    QString accentHover  = QStringLiteral("#2C8F84");
    QString accentPress  = QStringLiteral("#1A6960");
    QString accentText   = QStringLiteral("#1A6C63"); // 青绿当文字用时的取值
    QString accentSoft   = QStringLiteral("#E4F1EE"); // 选中/归属的淡薄荷
    QString accentSoftHi = QStringLiteral("#D6EAE5");
    QString accentEdge   = QStringLiteral("#C7E2DC");

    // ---- 语义色：只在真的发生时出现 ----
    QString successFg    = QStringLiteral("#12805A");
    QString successBg    = QStringLiteral("#E3F5EC");
    QString successEdge  = QStringLiteral("#C6E8D8");
    QString warningFg    = QStringLiteral("#A06410");
    QString warningBg    = QStringLiteral("#FBF0DF");
    QString warningEdge  = QStringLiteral("#F0DCBC");
    QString errorFg      = QStringLiteral("#C4483A");
    QString errorBg      = QStringLiteral("#FBEAE7");
    QString errorEdge    = QStringLiteral("#F3D4CE");
    QString neutralFg    = QStringLiteral("#5F7183");
    QString neutralBg    = QStringLiteral("#EFF3F4");
    QString neutralEdge  = QStringLiteral("#E2E9EB");

    QString tooltipBg    = QStringLiteral("#16283A");
    QString tooltipFg    = QStringLiteral("#F2F7F6");
};

/// 尺寸与字号（低密度档）。
///
/// 圆角是这套语言最容易被做丢的一环：只要有一处保持直角，整屏的柔软感就会塌掉。
/// 所以这里连输入框和表格选中态都有圆角，唯一的例外是分段控件的内接缝。
struct RagMetrics
{
    int space1 = 4;
    int space2 = 8;
    int space3 = 12;
    int space4 = 16;
    int space5 = 24;
    int space6 = 32;
    int space7 = 40;

    int radiusControl   = 12;
    int radiusContainer = 18;
    int radiusChip      = 14;
    int radiusTag       = 12; // 约等于标签自身高度的一半 → 全圆胶囊
    int radiusPill      = 19; // 约等于控件高度的一半 → 全圆胶囊

    int controlHeight   = 38;
    int primaryHeight   = 40;
    int navItemHeight   = 46;
    int rowHeight       = 40;
    int headerHeight    = 38;
    int iconChip        = 36; // 指标卡左上角那个淡彩方块
    // 区块标题行的固定高度。
    //
    // 不固定的后果：带状态标签的标题行比不带的高 6px，两栏并排时标题错位，
    // 而错位量取决于"这一栏这次有没有标签"——同一个页面在不同状态下对齐方式都不一样。
    int sectionHeaderHeight = 28;

    int cardPadH = 24;
    int cardPadV = 20;

    int fontTitle   = 24;
    int fontSection = 15;
    int fontBase    = 14;
    int fontSmall   = 13;
    int fontTiny    = 12;
    int fontMetric  = 36; // 参考稿里那个"120"，靠字号本身承担全部视觉重量

    // Segoe UI Variable 是 Windows 11 自带的可变字体，带光学尺寸：
    // Display 用于大字号（标题、指标数字），Text 用于正文，中文回退到雅黑。
    // 参考稿用的是几何圆体（Poppins 一类），Windows 上没有等价物，
    // Segoe UI Variable 是这个平台上最接近的选择，且不引入字体资源依赖。
    QString uiFamily      = QStringLiteral("'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI', 'Microsoft YaHei'");
    QString displayFamily = QStringLiteral("'Segoe UI Variable Display', 'Segoe UI', 'Microsoft YaHei UI', 'Microsoft YaHei'");
    QString monoFamily    = QStringLiteral("'Cascadia Mono', Consolas, 'Courier New', monospace");
};

/// 组装全局 QSS。
///
/// arrowDown / arrowUp / check 是箭头与对勾 PNG 的路径（见 ragui::arrowAsset / checkAsset）。
/// 留空则不写对应规则，退回 Qt 原生绘制——绝不能引用一个不存在的文件，那会渲染成空白。
/// 调用方通常用 ragui::buildThemeStyleSheet()，它会把这几个参数备好。
inline QString buildStyleSheet(const RagPalette &p = RagPalette(), const RagMetrics &m = RagMetrics(),
                               const QString &arrowDown = QString(),
                               const QString &arrowUp = QString(),
                               const QString &check = QString())
{
    QString css;
    css.reserve(16000);

    // ---------- 基础 ----------
    // 背景一律 transparent：薄荷底和白色浮层由 ragui::SurfaceBackground 自绘，
    // 给 QWidget 设实色会挡住浮层，也会泄漏进按钮和输入框内部。
    css += QStringLiteral("QWidget { background: transparent; color: %1;"
                          " font-family: %2; font-size: %3px; }")
               .arg(p.text, m.uiFamily).arg(m.fontBase);
    // 主窗与对话框铺薄荷底；状态栏在中央部件之外，靠这条规则拿到底色。
    css += QStringLiteral("QMainWindow { background: %1; }").arg(p.ground);
    css += QStringLiteral("QDialog { background: %1; }").arg(p.dialogGround);
    css += QStringLiteral("QToolTip { background: %1; color: %2; border: none;"
                          " border-radius: %3px; padding: 7px 11px; font-size: %4px; }")
               .arg(p.tooltipBg, p.tooltipFg).arg(m.radiusControl).arg(m.fontSmall);
    css += QStringLiteral("QStatusBar { background: transparent; color: %1; border: none;"
                          " min-height: 30px; padding-left: %2px; font-size: %3px; }")
               .arg(p.textMuted).arg(m.space5).arg(m.fontSmall);
    css += QStringLiteral("QStatusBar::item { border: none; }");

    // ---------- 容器 ----------
    // 白卡浮在白面板上，只靠一道发丝边区分。边再重一点就会变回"表单框"。
    css += QStringLiteral("QFrame#card { background: %1; border: 1px solid %2;"
                          " border-radius: %3px; }")
               .arg(p.surface, p.border).arg(m.radiusContainer);
    // 淡彩卡：参考稿里那张薄荷底的图表卡。用于"这一组内容属于同一件事"。
    css += QStringLiteral("QFrame#tintCard { background: %1; border: none;"
                          " border-radius: %2px; }").arg(p.tintMint).arg(m.radiusContainer);
    // 状态条不成卡：参考稿的顶部区域是裸露在白面板上的，没有第二层容器。
    css += QStringLiteral("QFrame#statusStrip { background: transparent; border: none; }");
    css += QStringLiteral("QFrame#inlineWell { background: %1; border: none;"
                          " border-radius: %2px; }").arg(p.surfaceMuted).arg(m.radiusControl);
    css += QStringLiteral("QFrame#toolbarRow { background: transparent; border: none; }");
    css += QStringLiteral("QFrame#hDivider { background: %1; border: none; max-height: 1px; }")
               .arg(p.divider);
    css += QStringLiteral("QFrame#vDivider { background: %1; border: none; max-width: 1px; }")
               .arg(p.divider);

    // ---------- 文字层级 ----------
    css += QStringLiteral("QLabel#pageTitle { color: %1; font-family: %2; font-size: %3px;"
                          " font-weight: 600; }")
               .arg(p.textStrong, m.displayFamily).arg(m.fontTitle);
    css += QStringLiteral("QLabel#sectionTitle { color: %1; font-size: %2px; font-weight: 600; }")
               .arg(p.textStrong).arg(m.fontSection);
    css += QStringLiteral("QLabel#sectionCaption { color: %1; font-size: %2px; }")
               .arg(p.textMuted).arg(m.fontSmall);
    css += QStringLiteral("QLabel#pageSubtitle, QLabel#mutedText { color: %1; font-size: %2px; }")
               .arg(p.textMuted).arg(m.fontSmall);
    css += QStringLiteral("QLabel#faintText { color: %1; font-size: %2px; }")
               .arg(p.textFaint).arg(m.fontSmall);
    css += QStringLiteral("QLabel#monoText { font-family: %1; font-size: %2px; color: %3; }")
               .arg(m.monoFamily).arg(m.fontSmall).arg(p.text);
    css += QStringLiteral("QLabel#emptyHint { color: %1; font-size: %2px; }")
               .arg(p.textMuted).arg(m.fontBase);
    css += QStringLiteral("QLabel:disabled { color: %1; }").arg(p.textFaint);

    // 指标：数字大到能单独立住，标注退成小字。参考稿的 "120 / 4 not confirmed" 就是这个结构。
    css += QStringLiteral("QLabel#metricValue, QLabel[metric=\"value\"]"
                          " { color: %1; font-family: %2; font-size: %3px; font-weight: 600; }")
               .arg(p.textStrong, m.displayFamily).arg(m.fontMetric);
    css += QStringLiteral("QLabel#metricLabel, QLabel[metric=\"caption\"]"
                          " { color: %1; font-size: %2px; font-weight: 500; }")
               .arg(p.textMuted).arg(m.fontSmall);
    css += QStringLiteral("QLabel[metric=\"title\"] { color: %1; font-size: %2px;"
                          " font-weight: 600; }").arg(p.textStrong).arg(m.fontBase);
    css += QStringLiteral("QLabel[metric=\"value\"][empty=\"true\"]"
                          " { color: %1; font-weight: 400; }").arg(p.textFaint);
    // 指标卡左上角的淡彩图标方块
    css += QStringLiteral("QLabel#metricIcon { background: %1; border: none; border-radius: %2px;"
                          " min-width: %3px; max-width: %3px; min-height: %3px; max-height: %3px; }")
               .arg(p.tintMint).arg(m.radiusControl).arg(m.iconChip);
    css += QStringLiteral("QLabel#metricIcon[tone=\"success\"] { background: %1; }").arg(p.successBg);
    css += QStringLiteral("QLabel#metricIcon[tone=\"warning\"] { background: %1; }").arg(p.warningBg);
    css += QStringLiteral("QLabel#metricIcon[tone=\"error\"] { background: %1; }").arg(p.errorBg);
    css += QStringLiteral("QLabel#metricIcon[tone=\"info\"] { background: %1; }").arg(p.tintLilac);

    // ---------- 状态标签 ----------
    // 全圆胶囊 + 淡彩底。预留 1px 透明边框，状态切换时尺寸不变，避免布局跳动。
    const QString tagBase =
        QStringLiteral(" border-radius: %1px; padding: 4px 12px; font-size: %2px; font-weight: 500;"
                       " border: 1px solid transparent; ")
            .arg(m.radiusTag).arg(m.fontSmall);
    css += QStringLiteral("QLabel#statusTag {%1}").arg(tagBase);
    css += QStringLiteral("QLabel#statusNeutral, QLabel#statusSuccess,"
                          " QLabel#statusWarning, QLabel#statusError {%1}").arg(tagBase);
    css += QStringLiteral("QLabel[tone=\"neutral\"], QLabel#statusNeutral"
                          " { background: %1; color: %2; border-color: %3; }")
               .arg(p.neutralBg, p.neutralFg, p.neutralEdge);
    css += QStringLiteral("QLabel[tone=\"success\"], QLabel#statusSuccess"
                          " { background: %1; color: %2; border-color: %3; }")
               .arg(p.successBg, p.successFg, p.successEdge);
    css += QStringLiteral("QLabel[tone=\"warning\"], QLabel#statusWarning"
                          " { background: %1; color: %2; border-color: %3; }")
               .arg(p.warningBg, p.warningFg, p.warningEdge);
    css += QStringLiteral("QLabel[tone=\"error\"], QLabel#statusError"
                          " { background: %1; color: %2; border-color: %3; }")
               .arg(p.errorBg, p.errorFg, p.errorEdge);
    css += QStringLiteral("QLabel[tone=\"info\"] { background: %1; color: %2; border-color: %3; }")
               .arg(p.accentSoft, p.accentText, p.accentEdge);

    // 行内状态：不带底色，只换文字颜色，几何尺寸不变，状态切换不引起布局跳动。
    css += QStringLiteral("QLabel#statusInline { color: %1; font-size: %2px; }")
               .arg(p.textMuted).arg(m.fontSmall);
    const QString inlineBase = QStringLiteral(" background: transparent; border-color: transparent; ");
    css += QStringLiteral("QLabel#statusInline[tone=\"neutral\"] {%1 color: %2; }")
               .arg(inlineBase, p.textMuted);
    css += QStringLiteral("QLabel#statusInline[tone=\"success\"] {%1 color: %2; }")
               .arg(inlineBase, p.successFg);
    css += QStringLiteral("QLabel#statusInline[tone=\"info\"] {%1 color: %2; }")
               .arg(inlineBase, p.accentText);
    css += QStringLiteral("QLabel#statusInline[tone=\"warning\"] {%1 color: %2; font-weight: 600; }")
               .arg(inlineBase, p.warningFg);
    css += QStringLiteral("QLabel#statusInline[tone=\"error\"] {%1 color: %2; font-weight: 600; }")
               .arg(inlineBase, p.errorFg);

    // ---------- 按钮 ----------
    // 基础规则就是次要按钮：白底 + 发丝边 + 全圆胶囊，等同参考稿右上角的 "This month" 筛选片。
    // 没起名的按钮也自动是这个长相。
    css += QStringLiteral("QPushButton { background: %1; color: %2; border: 1px solid %3;"
                          " border-radius: %4px; padding: 0 20px; min-height: %5px;"
                          " font-size: %6px; font-weight: 500; }")
               .arg(p.surface, p.text, p.borderStrong)
               .arg(m.radiusPill).arg(m.controlHeight).arg(m.fontBase);
    css += QStringLiteral("QPushButton:hover { background: %1; border-color: %2; }")
               .arg(p.surfaceMuted, p.accentEdge);
    css += QStringLiteral("QPushButton:pressed { background: %1; }").arg(p.surfaceHover);
    css += QStringLiteral("QPushButton:focus { border-color: %1; }").arg(p.accent);
    css += QStringLiteral("QPushButton:disabled { background: %1; color: %2; border-color: %3; }")
               .arg(p.surfaceMuted, p.textFaint, p.divider);
    // 主按钮：整屏唯一的实心青绿块，所以它一定是那个主操作。
    css += QStringLiteral("QPushButton#primaryButton, QPushButton#hardFaultButton,"
                          " QPushButton[variant=\"primary\"]"
                          " { background: %1; color: %2; border: 1px solid %1;"
                          " border-radius: %3px; min-height: %4px; font-weight: 600; }")
               .arg(p.accent, p.textOnAccent).arg(m.primaryHeight / 2).arg(m.primaryHeight);
    css += QStringLiteral("QPushButton#primaryButton:hover, QPushButton#hardFaultButton:hover,"
                          " QPushButton[variant=\"primary\"]:hover"
                          " { background: %1; border-color: %1; }").arg(p.accentHover);
    css += QStringLiteral("QPushButton#primaryButton:pressed, QPushButton#hardFaultButton:pressed,"
                          " QPushButton[variant=\"primary\"]:pressed"
                          " { background: %1; border-color: %1; }").arg(p.accentPress);
    css += QStringLiteral("QPushButton#primaryButton:disabled, QPushButton#hardFaultButton:disabled,"
                          " QPushButton[variant=\"primary\"]:disabled"
                          " { background: %1; color: %2; border-color: %1; }")
               .arg(p.neutralBg, p.textFaint);
    css += QStringLiteral("QPushButton#secondaryButton { background: %1; color: %2;"
                          " border-color: %3; }").arg(p.surface, p.text, p.borderStrong);
    // 淡薄荷按钮：比次要按钮更近一步，但不抢主操作。
    css += QStringLiteral("QPushButton[variant=\"soft\"] { background: %1; color: %2;"
                          " border-color: transparent; font-weight: 600; }")
               .arg(p.accentSoft, p.accentText);
    css += QStringLiteral("QPushButton[variant=\"soft\"]:hover { background: %1;"
                          " border-color: transparent; }").arg(p.accentSoftHi);
    css += QStringLiteral("QPushButton[variant=\"ghost\"] { background: transparent; color: %1;"
                          " border-color: transparent; }").arg(p.textMuted);
    css += QStringLiteral("QPushButton[variant=\"ghost\"]:hover { background: %1; color: %2;"
                          " border-color: transparent; }").arg(p.surfaceHover, p.textStrong);
    css += QStringLiteral("QPushButton[variant=\"danger\"] { background: %1; color: %2;"
                          " border-color: %3; }").arg(p.errorBg, p.errorFg, p.errorEdge);
    css += QStringLiteral("QPushButton[variant=\"danger\"]:hover { background: #F7DDD7;"
                          " border-color: %1; }").arg(p.errorFg);
    css += QStringLiteral("QPushButton[variant=\"link\"] { background: transparent; color: %1;"
                          " border-color: transparent; padding: 0 8px; font-weight: 600; }")
               .arg(p.accentText);
    css += QStringLiteral("QPushButton[variant=\"link\"]:hover { color: %1; }").arg(p.accent);
    // 变体的禁用态必须显式写，而且必须写在这些变体规则之后。
    //
    // 上面那条 QPushButton:disabled 和 QPushButton[variant="danger"] 特异性相同
    // （都是一个类型 + 一个限定），Qt 按"后写的赢"裁决，于是变体的颜色把禁用态整个盖掉：
    // 禁用的「拒绝候选」仍然是粉色的，看起来还能点。ghost / link 不给底色，
    // 它们本来就该是透明的，禁用时长出一个灰方块更奇怪。
    css += QStringLiteral("QPushButton[variant=\"soft\"]:disabled,"
                          " QPushButton[variant=\"danger\"]:disabled"
                          " { background: %1; color: %2; border-color: transparent; }")
               .arg(p.neutralBg, p.textFaint);
    css += QStringLiteral("QPushButton[variant=\"ghost\"]:disabled,"
                          " QPushButton[variant=\"link\"]:disabled"
                          " { background: transparent; color: %1; border-color: transparent; }")
               .arg(p.textFaint);
    css += QStringLiteral("QToolButton { background: transparent; border: none; padding: 6px;"
                          " border-radius: %1px; }").arg(m.radiusControl);
    css += QStringLiteral("QToolButton:hover { background: %1; }").arg(p.surfaceHover);

    // ---------- 分段控件 ----------
    // 一条淡色轨道，被选中的那一格是实心青绿。轨道两端是圆的，内接缝保持直角——
    // 这是全局唯一允许出现直角的地方。
    css += QStringLiteral("QPushButton[segment] { background: %1; color: %2; border: none;"
                          " padding: 0 20px; min-height: %3px; border-radius: 0;"
                          " font-size: %4px; font-weight: 500; }")
               .arg(p.surfaceMuted, p.textMuted).arg(m.controlHeight).arg(m.fontBase);
    css += QStringLiteral("QPushButton[segment=\"first\"]"
                          " { border-top-left-radius: %1px; border-bottom-left-radius: %1px; }")
               .arg(m.radiusPill);
    css += QStringLiteral("QPushButton[segment=\"last\"]"
                          " { border-top-right-radius: %1px; border-bottom-right-radius: %1px; }")
               .arg(m.radiusPill);
    css += QStringLiteral("QPushButton[segment]:hover { background: %1; color: %2; }")
               .arg(p.surfaceHover, p.textStrong);
    css += QStringLiteral("QPushButton[segment]:checked { background: %1; color: %2;"
                          " font-weight: 600; }").arg(p.accent, p.textOnAccent);
    css += QStringLiteral("QPushButton[segment]:checked:hover { background: %1; }").arg(p.accentHover);
    css += QStringLiteral("QPushButton[segment]:disabled { color: %1; }").arg(p.textFaint);

    // ---------- 输入控件 ----------
    // 参考稿的搜索框是"只有填色、没有边"的。这里边框取值几乎等于填色，
    // 所以读起来是无边的，但在纯白背景上仍有轮廓。
    css += QStringLiteral("QPlainTextEdit, QTextEdit, QTextBrowser, QLineEdit, QSpinBox,"
                          " QDoubleSpinBox, QComboBox {"
                          " background: %1; color: %2; border: 1px solid %3;"
                          " border-radius: %4px; selection-background-color: %5; selection-color: %6; }")
               .arg(p.surfaceMuted, p.text, p.divider).arg(m.radiusControl)
               .arg(p.accentSoftHi, p.textStrong);
    css += QStringLiteral("QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox"
                          " { min-height: %1px; padding: 0 14px; }").arg(m.controlHeight - 2);
    css += QStringLiteral("QPlainTextEdit, QTextEdit, QTextBrowser { padding: 12px 14px; }");
    css += QStringLiteral("QPlainTextEdit:focus, QTextEdit:focus, QTextBrowser:focus,"
                          " QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus"
                          " { border-color: %1; background: %2; }").arg(p.accent, p.surface);
    css += QStringLiteral("QLineEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover, QComboBox:hover"
                          " { border-color: %1; }").arg(p.borderStrong);
    css += QStringLiteral("QPlainTextEdit:disabled, QTextEdit:disabled, QTextBrowser:disabled,"
                          " QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled,"
                          " QComboBox:disabled { background: %1; color: %2; border-color: %3; }")
               .arg(p.neutralBg, p.textFaint, p.divider);
    css += QStringLiteral("QPlainTextEdit[invalid=\"true\"], QTextEdit[invalid=\"true\"],"
                          " QLineEdit[invalid=\"true\"] { border-color: %1; background: %2; }")
               .arg(p.errorFg, p.errorBg);
    // 填色 = 可以输入。这是全局约定，也是这套设计语言的成败手：
    // 只读的展示区（诊断结论、原始日志、运行日志）如果也填色，一整块 600×300 的
    // 空浅灰就会成为屏幕上最大的物体，而它什么都不表示。参考稿里所有大面积浅色块
    // 都是装着内容的卡，没有一块是空的输入框。
    css += QStringLiteral("QPlainTextEdit:read-only, QTextEdit:read-only, QTextBrowser {"
                          " background: transparent; border: none; padding: 2px 0; }");
    css += QStringLiteral("QPlainTextEdit:read-only:focus, QTextEdit:read-only:focus,"
                          " QTextBrowser:focus { border: none; background: transparent; }");
    css += QStringLiteral("QPlainTextEdit#monoEdit, QTextBrowser#monoEdit,"
                          " QPlainTextEdit#runtimeActivityLog"
                          " { font-family: %1; font-size: %2px; }")
               .arg(m.monoFamily).arg(m.fontSmall);
    css += QStringLiteral("QComboBox QAbstractItemView { background: %1; color: %2;"
                          " border: 1px solid %3; border-radius: %4px; padding: 6px;"
                          " selection-background-color: %5; selection-color: %6; outline: none; }")
               .arg(p.surface, p.text, p.border).arg(m.radiusControl)
               .arg(p.accentSoft, p.accentText);

    // ---------- 下拉与微调的箭头 ----------
    // 见 ragui::arrowAsset() 里的说明：这两个子控件只能用 image: url() 给图。
    if (!arrowDown.isEmpty()) {
        css += QStringLiteral("QComboBox::drop-down { subcontrol-origin: padding;"
                              " subcontrol-position: center right; width: 30px;"
                              " border: none; background: transparent; }");
        css += QStringLiteral("QComboBox::down-arrow { image: url(\"%1\");"
                              " width: 10px; height: 10px; }").arg(arrowDown);
        css += QStringLiteral("QComboBox { padding-right: 30px; }");
    }
    if (!arrowDown.isEmpty() && !arrowUp.isEmpty()) {
        const QString spinButton =
            QStringLiteral(" width: 22px; height: %1px; border: none; background: transparent;"
                           " border-radius: 7px; ").arg((m.controlHeight - 8) / 2);
        css += QStringLiteral("QSpinBox::up-button, QDoubleSpinBox::up-button"
                              " { subcontrol-origin: border; subcontrol-position: top right;"
                              " margin: 3px 4px 0 0;%1}").arg(spinButton);
        css += QStringLiteral("QSpinBox::down-button, QDoubleSpinBox::down-button"
                              " { subcontrol-origin: border; subcontrol-position: bottom right;"
                              " margin: 0 4px 3px 0;%1}").arg(spinButton);
        css += QStringLiteral("QSpinBox::up-button:hover, QSpinBox::down-button:hover,"
                              " QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover"
                              " { background: %1; }").arg(p.accentSoft);
        css += QStringLiteral("QSpinBox::up-arrow, QDoubleSpinBox::up-arrow"
                              " { image: url(\"%1\"); width: 10px; height: 10px; }").arg(arrowUp);
        css += QStringLiteral("QSpinBox::down-arrow, QDoubleSpinBox::down-arrow"
                              " { image: url(\"%1\"); width: 10px; height: 10px; }").arg(arrowDown);
        css += QStringLiteral("QSpinBox, QDoubleSpinBox { padding-right: 32px; }");
    }

    css += QStringLiteral("QCheckBox { spacing: 10px; color: %1; }").arg(p.text);
    // 勾选框在两个地方出现：独立的 QCheckBox，以及表格/列表单元格里的 check indicator。
    // 后者一旦被 QSS 接管却没有规则，不会退回原生外观，而是渲染成一枚纯黑实心方块——
    // 在这套浅色配色里那是全屏唯一的黑色。两者必须一起声明。
    const QString indicatorBase =
        QStringLiteral(" width: 18px; height: 18px; border: 1px solid %1; border-radius: 6px;"
                       " background: %2; ").arg(p.borderStrong, p.surface);
    css += QStringLiteral("QCheckBox::indicator, QTableView::indicator, QTreeView::indicator,"
                          " QListView::indicator {%1}").arg(indicatorBase);
    css += QStringLiteral("QCheckBox::indicator:hover, QTableView::indicator:hover,"
                          " QTreeView::indicator:hover, QListView::indicator:hover"
                          " { border-color: %1; }").arg(p.accent);
    css += QStringLiteral("QCheckBox::indicator:checked, QTableView::indicator:checked,"
                          " QTreeView::indicator:checked, QListView::indicator:checked"
                          " { background: %1; border-color: %1;%2 }")
               .arg(p.accent, check.isEmpty() ? QString()
                                              : QStringLiteral(" image: url(\"%1\");").arg(check));
    css += QStringLiteral("QCheckBox::indicator:disabled, QTableView::indicator:disabled,"
                          " QTreeView::indicator:disabled, QListView::indicator:disabled"
                          " { background: %1; border-color: %2; }")
               .arg(p.neutralBg, p.divider);
    css += QStringLiteral("QCheckBox:disabled { color: %1; }").arg(p.textFaint);

    css += QStringLiteral("QGroupBox { border: 1px solid %1; border-radius: %2px; background: %3;"
                          " margin-top: 14px; padding: %4px %5px %5px %5px; }")
               .arg(p.border).arg(m.radiusContainer).arg(p.surface)
               .arg(m.space5).arg(m.space5);
    css += QStringLiteral("QGroupBox::title { subcontrol-origin: margin; left: %1px; padding: 0 8px;"
                          " color: %2; font-size: %3px; font-weight: 600; }")
               .arg(m.space4).arg(p.textStrong).arg(m.fontSection);

    // ---------- 表格 ----------
    // 表格不再是"一个框"。它直接铺在白面板上：没有外框、没有网格线，
    // 表头只留一条底部发丝线，行与行之间靠 40px 的行高呼吸。
    // 这是"极简"在数据密集处的具体做法——线越少，读得越快。
    css += QStringLiteral("QTableWidget, QTableView, QTreeView, QListView, QListWidget {"
                          " background: transparent; color: %1; border: none;"
                          " gridline-color: transparent; alternate-background-color: %2;"
                          " selection-background-color: %3; selection-color: %4; outline: none; }")
               .arg(p.text, p.surfaceAlt, p.accentSoft, p.textStrong);
    css += QStringLiteral("QTableView::item, QTreeView::item, QListView::item"
                          " { padding: 8px 14px; border: none; }");
    css += QStringLiteral("QTableView::item:hover, QListView::item:hover { background: %1; }")
               .arg(p.surfaceHover);
    css += QStringLiteral("QTableView::item:selected, QListView::item:selected"
                          " { background: %1; color: %2; }").arg(p.accentSoft, p.textStrong);
    css += QStringLiteral("QHeaderView { background: transparent; border: none; }");
    css += QStringLiteral("QHeaderView::section { background: transparent; color: %1;"
                          " border: none; border-bottom: 1px solid %2; padding: 8px 14px;"
                          " font-size: %3px; font-weight: 600; }")
               .arg(p.textMuted, p.divider).arg(m.fontTiny);
    css += QStringLiteral("QHeaderView::section:hover { color: %1; }").arg(p.textStrong);
    css += QStringLiteral("QTableCornerButton::section { background: transparent; border: none;"
                          " border-bottom: 1px solid %1; }").arg(p.divider);
    // 用 setItemWidget() 装内嵌控件的列表：通用的 8px 竖直内边距会把行挤扁，
    // 于是控件被裁、标签和文字叠在一起。这类列表的行高由控件自己决定，
    // 内边距必须让位。
    css += QStringLiteral("QListWidget#runtimeHealthIssues { background: transparent;"
                          " border: none; }");
    css += QStringLiteral("QListWidget#runtimeHealthIssues::item { padding: 2px 4px; }");

    // ---------- 滚动条 ----------
    // 参考稿里看不到滚动条。这里做到"需要时才看得见"：细、圆、贴边、无按钮。
    css += QStringLiteral("QScrollBar:vertical { background: transparent; width: 10px;"
                          " margin: 2px 0; }");
    css += QStringLiteral("QScrollBar::handle:vertical { background: %1; border-radius: 4px;"
                          " min-height: 36px; }").arg(p.borderStrong);
    css += QStringLiteral("QScrollBar::handle:vertical:hover { background: %1; }").arg(p.textFaint);
    css += QStringLiteral("QScrollBar:horizontal { background: transparent; height: 10px;"
                          " margin: 0 2px; }");
    css += QStringLiteral("QScrollBar::handle:horizontal { background: %1; border-radius: 4px;"
                          " min-width: 36px; }").arg(p.borderStrong);
    css += QStringLiteral("QScrollBar::handle:horizontal:hover { background: %1; }").arg(p.textFaint);
    css += QStringLiteral("QScrollBar::add-line, QScrollBar::sub-line"
                          " { width: 0; height: 0; background: transparent; border: none; }");
    css += QStringLiteral("QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }");
    css += QStringLiteral("QAbstractScrollArea::corner { background: transparent; border: none; }");

    // ---------- 分隔器 ----------
    css += QStringLiteral("QSplitter::handle { background: transparent; }");
    css += QStringLiteral("QSplitter::handle:hover { background: %1; }").arg(p.accentEdge);
    css += QStringLiteral("QSplitter::handle:horizontal { width: %1px; }").arg(m.space3);
    css += QStringLiteral("QSplitter::handle:vertical { height: %1px; }").arg(m.space3);

    // ---------- 菜单 ----------
    css += QStringLiteral("QMenu { background: %1; color: %2; border: 1px solid %3;"
                          " border-radius: %4px; padding: 8px; }")
               .arg(p.surface, p.text, p.border).arg(m.radiusContainer);
    css += QStringLiteral("QMenu::item { padding: 8px 30px 8px 16px; border-radius: %1px; }")
               .arg(m.radiusControl);
    css += QStringLiteral("QMenu::item:selected { background: %1; color: %2; }")
               .arg(p.accentSoft, p.accentText);
    css += QStringLiteral("QMenu::separator { height: 1px; background: %1; margin: 8px 10px; }")
               .arg(p.divider);

    // ---------- 导航轨 ----------
    // 参考稿的左轨只有一条发丝分界线，没有底色——它和主内容在同一块白面板上。
    // 选中项是一小片圆角薄荷，不是高亮条，也不是更深的背景板。
    css += QStringLiteral("QFrame#navigationRail { background: transparent; border: none;"
                          " border-right: 1px solid %1; }").arg(p.divider);
    css += QStringLiteral("QLabel#navigationBrand { color: %1; font-family: %2; font-size: %3px;"
                          " font-weight: 600; }")
               .arg(p.textStrong, m.displayFamily).arg(m.fontSection + 2);
    css += QStringLiteral("QLabel#navigationCaption { color: %1; font-size: %2px; }")
               .arg(p.textMuted).arg(m.fontTiny);
    css += QStringLiteral("QLabel#navigationFootnote { color: %1; font-size: %2px; }")
               .arg(p.textFaint).arg(m.fontTiny);
    css += QStringLiteral("QLabel#navigationSectionLabel { color: %1; font-size: %2px;"
                          " font-weight: 600; }").arg(p.textFaint).arg(m.fontTiny);
    css += QStringLiteral("QPushButton#navigationButton { background: transparent; color: %1;"
                          " border: none; border-radius: %2px; text-align: left;"
                          " padding: 0 14px; min-height: %3px; font-size: %4px; font-weight: 500; }")
               .arg(p.textMuted).arg(m.radiusChip).arg(m.navItemHeight).arg(m.fontBase);
    css += QStringLiteral("QPushButton#navigationButton:hover { background: %1; color: %2; }")
               .arg(p.surfaceHover, p.textStrong);
    css += QStringLiteral("QPushButton#navigationButton[active=\"true\"] { background: %1;"
                          " color: %2; font-weight: 600; }").arg(p.accentSoft, p.accentText);
    css += QStringLiteral("QPushButton#navigationButtonActive { background: %1; color: %2;"
                          " border: none; border-radius: %3px; text-align: left; padding: 0 14px;"
                          " min-height: %4px; font-size: %5px; font-weight: 600; }")
               .arg(p.accentSoft, p.accentText).arg(m.radiusChip)
               .arg(m.navItemHeight).arg(m.fontBase);

    return css;
}

} // namespace ragui

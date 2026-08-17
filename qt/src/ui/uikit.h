#pragma once

// RAG 诊断工作台 · 共享控件工厂
//
// 改版前 makeCard() / makeSectionTitle() / setStatusPill() 在四个页面各复制了一份，
// 结果是同一个概念在不同页面长得不一样。这里是唯一的实现。
//
// 全部 header-only（inline / 无 Q_OBJECT），所以不需要动 CMakeLists —— qt/CMakeLists.txt
// 里 8 个 smoke target 各自显式列源文件，新增 .cpp 要改 9 处。
//
// 版式铁律（四个页面都要遵守）：
//   1. 全部内容都在一块白色圆角浮层里（ragui::SurfaceBackground 自绘）。
//      浮层已经是第一层容器，所以页面里最多再有一层——卡片和表格二选一，不叠加。
//   2. 表格直接铺在白底上：无外框、无网格线，只有表头一条发丝线。不要套 QFrame#card。
//   3. 工具条、过滤条、统计条一律用 makeToolbar()，不要各自成卡。
//   4. 长文本（路径 / Chunk ID / 模型名 / 元信息）一律用 ElidedLabel，
//      它的水平 sizePolicy 是 Ignored，不会把 QSplitter 的比例绑架掉。
//   5. 按状态换外观用 setTag() / setInvalid() 这类动态属性 + repolish()，
//      绝不用控件级 setStyleSheet()——那会把圆角和内边距一起冲掉，控件会跳变。
//   6. 没有直角。新增控件如果自带方角，去 theme.h 里补圆角规则，不要就地 setStyleSheet。

#include "ui/theme.h"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QBrush>
#include <QColor>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointF>
#include <QPushButton>
#include <QRectF>
#include <QResizeEvent>
#include <QSize>
#include <QSizePolicy>
#include <QStandardPaths>
#include <QString>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace ragui {

// ---------------------------------------------------------------- 基础工具

/// 动态属性改完必须重新抛光，否则 QSS 不会重新求值。
inline void repolish(QWidget *widget)
{
    if (!widget) {
        return;
    }
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

/// 设置动态属性并立即生效。
inline void setDynamicProperty(QWidget *widget, const char *name, const QVariant &value)
{
    if (!widget) {
        return;
    }
    widget->setProperty(name, value);
    repolish(widget);
}

/// 输入校验失败态。取代 setStyleSheet("border: 1px solid red")。
inline void setInvalid(QWidget *widget, bool invalid)
{
    setDynamicProperty(widget, "invalid", invalid ? QStringLiteral("true") : QString());
}

// ---------------------------------------------------------------- 图标

/// 线性图标集。
///
/// 全部用 QPainter 现画，不引入任何图标字体或 svg 资源：
///   · 图标字体（Segoe Fluent Icons 之类）靠私有区码位取字，码位记错就渲染成方框，
///     而且离屏渲染环境里字体不一定装得上——离屏截图验收就废了；
///   · svg 资源要动 qrc 和 CMakeLists，而 qt/CMakeLists.txt 里 12 个 target
///     各自显式列源文件，加一个资源要改十几处。
/// 现画的代价是每个图标十来行，换来的是零依赖、可离屏验收、颜色随状态即时切换。
///
/// 造型约束：统一 24 单位画布、纯描边、圆头圆角、线宽 2/24。
/// 不做实心、不做双色、不做拟物——参考稿的图标就是这个规格。
enum class Glyph {
    Pulse,   ///< 诊断：心电波形
    List,    ///< 案例库：三行条目
    Book,    ///< 知识源：书
    Chart,   ///< 评估：柱状图
    Sliders, ///< 设置：两条推子
};

/// 把图标画进 pixmap。size 是逻辑像素，内部按屏幕缩放比出图，高分屏不糊。
inline QPixmap renderGlyph(Glyph glyph, const QColor &color, int size = 22, qreal dpr = 0.0)
{
    if (dpr <= 0.0) {
        dpr = qGuiApp ? qGuiApp->devicePixelRatio() : 1.0;
    }
    QPixmap pixmap(QSize(size, size) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // 24 单位画布 → 目标尺寸，之后所有坐标都可以照着 24 格写。
    painter.scale(size / 24.0, size / 24.0);

    QPen pen(color);
    pen.setWidthF(2.0);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    switch (glyph) {
    case Glyph::Pulse: {
        QPainterPath path;
        path.moveTo(3.0, 12.0);
        path.lineTo(7.6, 12.0);
        path.lineTo(10.0, 6.4);
        path.lineTo(13.6, 17.6);
        path.lineTo(16.0, 12.0);
        path.lineTo(21.0, 12.0);
        painter.drawPath(path);
        break;
    }
    case Glyph::List: {
        for (int row = 0; row < 3; ++row) {
            const qreal y = 6.5 + row * 5.5;
            painter.drawLine(QPointF(9.0, y), QPointF(20.5, y));
            painter.setBrush(color);
            painter.drawEllipse(QPointF(4.6, y), 1.35, 1.35);
            painter.setBrush(Qt::NoBrush);
        }
        break;
    }
    case Glyph::Book: {
        painter.drawRoundedRect(QRectF(3.5, 4.0, 17.0, 16.0), 2.8, 2.8);
        painter.drawLine(QPointF(8.4, 4.0), QPointF(8.4, 20.0)); // 书脊
        painter.drawLine(QPointF(11.8, 9.2), QPointF(17.2, 9.2));
        painter.drawLine(QPointF(11.8, 13.0), QPointF(17.2, 13.0));
        break;
    }
    case Glyph::Chart: {
        pen.setWidthF(2.6);
        painter.setPen(pen);
        painter.drawLine(QPointF(6.0, 19.0), QPointF(6.0, 13.4));
        painter.drawLine(QPointF(12.0, 19.0), QPointF(12.0, 6.6));
        painter.drawLine(QPointF(18.0, 19.0), QPointF(18.0, 10.2));
        break;
    }
    case Glyph::Sliders: {
        painter.drawLine(QPointF(3.5, 8.6), QPointF(20.5, 8.6));
        painter.drawLine(QPointF(3.5, 15.4), QPointF(20.5, 15.4));
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(9.0, 8.6), 2.9, 2.9);
        painter.drawEllipse(QPointF(15.0, 15.4), 2.9, 2.9);
        break;
    }
    }
    return pixmap;
}

inline QIcon makeGlyphIcon(Glyph glyph, const QString &color, int size = 22)
{
    return QIcon(renderGlyph(glyph, QColor(color), size));
}

/// 下拉框 / 微调框的箭头图片，返回可喂给 QSS `image: url(...)` 的路径。
///
/// 为什么要落成文件：QSS 的箭头子控件只认 image: url()。
///   · 自绘三角（transparent-border 那个 CSS 技法）在 Qt 的子控件绘制里会变成实心方块；
///   · 只声明按钮区宽度、不给图，又会把原生箭头整个抑制掉，下拉框看起来像没有下拉。
/// 两条路都试过。所以这里启动时把箭头画成 PNG 落到缓存目录再喂给样式表——
/// 颜色、线宽、圆头都由我们控制，和 renderGlyph() 是同一套造型规格。
///
/// 拿不到可写目录就返回空串，调用方据此跳过箭头规则、退回 Qt 原生绘制。
/// 绝不能让样式表引用一个不存在的文件——那会渲染成空白，比原生箭头更糟。
inline QString arrowAsset(bool pointingUp, const QString &color = RagPalette().textMuted)
{
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cacheDir.isEmpty() || !QDir().mkpath(cacheDir)) {
        return QString();
    }
    const QString file = QDir(cacheDir).filePath(
        QStringLiteral("chevron-%1-%2.png")
            .arg(pointingUp ? QStringLiteral("up") : QStringLiteral("down"),
                 QString(color).remove(QLatin1Char('#'))));
    const QString url = QDir::fromNativeSeparators(file);
    if (QFileInfo::exists(file)) {
        return url;
    }

    // 20px 出图、QSS 缩到 10px 显示，高分屏不糊。
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // 花括号初始化：QPen pen(QColor(color)) 会被解析成函数声明（most vexing parse）。
    QPen pen{QColor(color)};
    pen.setWidthF(2.4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    QPainterPath chevron;
    if (pointingUp) {
        chevron.moveTo(4.6, 12.4);
        chevron.lineTo(10.0, 7.0);
        chevron.lineTo(15.4, 12.4);
    } else {
        chevron.moveTo(4.6, 7.6);
        chevron.lineTo(10.0, 13.0);
        chevron.lineTo(15.4, 7.6);
    }
    painter.drawPath(chevron);
    painter.end();
    return pixmap.save(file, "PNG") ? url : QString();
}

/// 勾选框选中态的对勾。理由同 arrowAsset()：QSS 的 ::indicator 只认 image: url()。
/// 没有它，选中的勾选框就是一块纯色方块，看不出"勾上了"还是"被禁用了"。
inline QString checkAsset(const QString &color = RagPalette().textOnAccent)
{
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cacheDir.isEmpty() || !QDir().mkpath(cacheDir)) {
        return QString();
    }
    const QString file = QDir(cacheDir).filePath(
        QStringLiteral("check-%1.png").arg(QString(color).remove(QLatin1Char('#'))));
    const QString url = QDir::fromNativeSeparators(file);
    if (QFileInfo::exists(file)) {
        return url;
    }

    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen{QColor(color)};
    pen.setWidthF(2.6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    QPainterPath tick;
    tick.moveTo(4.8, 10.4);
    tick.lineTo(8.6, 14.2);
    tick.lineTo(15.2, 6.2);
    painter.drawPath(tick);
    painter.end();
    return pixmap.save(file, "PNG") ? url : QString();
}

/// 全局样式表 + 图片资源。这是 applyTheme() 该调用的那一个。
inline QString buildThemeStyleSheet()
{
    const RagPalette palette;
    return buildStyleSheet(palette, RagMetrics(), arrowAsset(false, palette.textMuted),
                           arrowAsset(true, palette.textMuted), checkAsset(palette.textOnAccent));
}

// ---------------------------------------------------------------- 状态标签

enum class Tone { Neutral, Success, Warning, Error, Info };

inline QString toneName(Tone tone)
{
    switch (tone) {
    case Tone::Success: return QStringLiteral("success");
    case Tone::Warning: return QStringLiteral("warning");
    case Tone::Error:   return QStringLiteral("error");
    case Tone::Info:    return QStringLiteral("info");
    case Tone::Neutral: break;
    }
    return QStringLiteral("neutral");
}

/// 兼容改版前的 objectName 写法（"statusSuccess" 等），便于逐步迁移调用点。
inline Tone toneFromLegacyName(const QString &legacyObjectName)
{
    if (legacyObjectName == QStringLiteral("statusSuccess")) return Tone::Success;
    if (legacyObjectName == QStringLiteral("statusWarning")) return Tone::Warning;
    if (legacyObjectName == QStringLiteral("statusError"))   return Tone::Error;
    if (legacyObjectName == QStringLiteral("statusInfo"))    return Tone::Info;
    return Tone::Neutral;
}

/// 语义色的直接取值。
///
/// 需要它的地方都是 QSS 够不到的：QTableWidgetItem 不是 QWidget，自绘图标也拿不到样式表。
/// 除这两类以外，颜色一律走 QSS 的 tone 动态属性，不要在代码里写死。
inline QString toneColor(Tone tone)
{
    const RagPalette palette;
    switch (tone) {
    case Tone::Success: return palette.successFg;
    case Tone::Warning: return palette.warningFg;
    case Tone::Error:   return palette.errorFg;
    case Tone::Info:    return palette.accentText;
    case Tone::Neutral: break;
    }
    return palette.text;
}

/// 更新状态标签的文案与语义色。objectName 保持稳定，方便测试选择器。
inline void setTag(QLabel *label, const QString &text, Tone tone)
{
    if (!label) {
        return;
    }
    label->setText(text);
    label->setProperty("tone", toneName(tone));
    repolish(label);
}

inline void setTag(QLabel *label, const QString &text, const QString &legacyObjectName)
{
    setTag(label, text, toneFromLegacyName(legacyObjectName));
}

inline QLabel *makeTag(const QString &text = QString(), Tone tone = Tone::Neutral,
                       const QString &objectName = QStringLiteral("statusTag"))
{
    auto *label = new QLabel;
    label->setObjectName(objectName);
    label->setAlignment(Qt::AlignCenter);
    setTag(label, text, tone);
    return label;
}

/// 计数标签的语义色：0 一律降级成中性灰。
///
/// 「待重建 0 / 缺失 0 / 错误 0」全是橙红色是纯噪声——只有真的发生了才该有颜色。
inline Tone countTone(int count, Tone toneWhenPositive)
{
    return count > 0 ? toneWhenPositive : Tone::Neutral;
}

/// 带长度上限的状态标签：超长的错误信息不会把标签撑成一整条色带，完整内容进 tooltip。
///
/// 标签不能用 ElidedLabel（它的水平 sizePolicy 是 Ignored，色块会铺满整格），
/// 所以这里按字符数截断。
inline void setTag(QLabel *label, const QString &text, Tone tone, int maxChars)
{
    if (!label) {
        return;
    }
    const QString shown =
        (maxChars > 0 && text.size() > maxChars) ? text.left(maxChars - 1) + QChar(0x2026) : text;
    setTag(label, shown, tone);
    label->setToolTip(text.size() > shown.size() ? text : QString());
}

/// 让控件按自身宽度收紧，不要在 QFormLayout / 拉伸布局里被拉成整行。
/// 状态标签放进表单时最需要它，否则会变成一条通栏色带。
inline QWidget *hugLeft(QWidget *widget, int trailingStretch = 1)
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(widget);
    if (trailingStretch > 0) {
        layout->addStretch(trailingStretch);
    }
    return row;
}

/// 不带底色的行内状态。平时用它——在这套设计语言里填色等于"可点/可编辑"，
/// 一个灰胶囊夹在按钮之间会被读成禁用按钮。语义色由 QLabel#statusInline[tone] 只改文字颜色，
/// 几何尺寸不变，所以状态切换不会引起布局跳动。真的需要一眼看见的异常态再换成 makeTag()。
inline QLabel *makeInlineStatus(const QString &text = QString())
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("statusInline"));
    return label;
}

// ---------------------------------------------------------------- 文本

/// 自动省略的单行标签。
///
/// 水平 sizePolicy 固定为 Ignored —— 这正是改版前案例库的病根：
/// 一个 setWordWrap(false) 的长 QLabel 把 QSplitter 的最小宽度顶到 750px，
/// 导致左侧案例列表只剩 200px、六列有四列要横向滚动才看得到。
class ElidedLabel : public QLabel
{
public:
    explicit ElidedLabel(QWidget *parent = nullptr)
        : QLabel(parent)
    {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setMinimumWidth(0);
        setWordWrap(false);
    }

    /// 设置完整文本；显示时按当前宽度省略，完整内容进 tooltip。
    void setFullText(const QString &text)
    {
        if (fullText_ == text) {
            return;
        }
        fullText_ = text;
        setToolTip(text);
        refresh();
    }

    QString fullText() const { return fullText_; }

    void setElideMode(Qt::TextElideMode mode)
    {
        elideMode_ = mode;
        refresh();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        refresh();
    }

private:
    void refresh()
    {
        const QString elided =
            fullText_.isEmpty() ? QString()
                                : fontMetrics().elidedText(fullText_, elideMode_, qMax(0, width()));
        if (elided != text()) {
            QLabel::setText(elided);
        }
    }

    QString fullText_;
    Qt::TextElideMode elideMode_ = Qt::ElideRight;
};

inline ElidedLabel *makeElidedLabel(const QString &text = QString(),
                                    const QString &objectName = QStringLiteral("mutedText"))
{
    auto *label = new ElidedLabel;
    label->setObjectName(objectName);
    label->setFullText(text);
    return label;
}

inline QLabel *makeMonoLabel(const QString &text = QString())
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("monoText"));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

// ---------------------------------------------------------------- 容器

/// 内容卡。只在真正需要成组的内容上用（结论区、Evidence 区、主从面板）。
/// 工具条、过滤条、统计条不要用它——那会变成一屏五六张白卡。
inline QFrame *makeCard(QWidget *content, int padH = -1, int padV = -1)
{
    const RagMetrics metrics;
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("card"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(padH < 0 ? metrics.cardPadH : padH,
                               padV < 0 ? metrics.cardPadV : padV,
                               padH < 0 ? metrics.cardPadH : padH,
                               padV < 0 ? metrics.cardPadV : padV);
    layout->setSpacing(metrics.space2);
    if (content) {
        layout->addWidget(content);
    }
    return card;
}

/// 无卡片的横向工具条。用于模式切换、过滤、页面级操作。
inline QWidget *makeToolbar(bool bottomRule = false)
{
    const RagMetrics metrics;
    auto *row = new QFrame;
    row->setObjectName(bottomRule ? QStringLiteral("toolbarRow") : QString());
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, bottomRule ? metrics.space2 : 0);
    layout->setSpacing(metrics.space2);
    return row;
}

inline QFrame *makeHDivider()
{
    auto *line = new QFrame;
    line->setObjectName(QStringLiteral("hDivider"));
    line->setFixedHeight(1);
    return line;
}

inline QFrame *makeVDivider()
{
    auto *line = new QFrame;
    line->setObjectName(QStringLiteral("vDivider"));
    line->setFixedWidth(1);
    return line;
}

/// 区块标题（可带灰色副标题）。
///
/// 改版前这里是 "<span class='sectionTitle'>" 富文本 + QSS 里的 `QLabel .sectionTitle`，
/// 但那在 QSS 里是「QLabel 内部一个名叫 sectionTitle 的控件」的后代选择器，永远匹配不到，
/// 所以标题和副标题一直渲染成同一种字。现在改成两个独立 QLabel + 真实 objectName。
///
/// 行高固定：带状态标签的标题行天生比不带的高，两栏并排时标题会错位，
/// 而错位量取决于"这一栏这次恰好有没有标签"。固定成一个值，对齐就与内容无关了。
inline QWidget *makeSectionTitle(const QString &title, const QString &caption = QString())
{
    const RagMetrics metrics;
    auto *row = new QWidget;
    row->setMinimumHeight(metrics.sectionHeaderHeight);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(metrics.space2);
    auto *titleLabel = new QLabel(title);
    titleLabel->setObjectName(QStringLiteral("sectionTitle"));
    layout->addWidget(titleLabel);
    if (!caption.isEmpty()) {
        auto *captionLabel = makeElidedLabel(caption, QStringLiteral("sectionCaption"));
        layout->addWidget(captionLabel, 1);
    } else {
        layout->addStretch(1);
    }
    return row;
}

/// 区块标题 + 右侧尾随控件（状态标签、小按钮等）。
inline QWidget *makeSectionHeader(const QString &title, const QString &caption, QWidget *trailing)
{
    const RagMetrics metrics;
    auto *row = new QWidget;
    row->setMinimumHeight(metrics.sectionHeaderHeight);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(metrics.space2);
    layout->addWidget(makeSectionTitle(title, caption), 1);
    if (trailing) {
        layout->addWidget(trailing, 0, Qt::AlignRight | Qt::AlignVCenter);
    }
    return row;
}

/// 指标块：大数字 + 小标注。评估中心的 Hit Rate / Recall / MRR 用它，
/// 不要再把指标写成一句流水账文本。
///
/// valueObjectName 用来给数值标签一个稳定选择器（测试按 objectName 定位，不按文案）。
/// 样式走 metric 动态属性，所以 objectName 可以自由命名而不丢外观。
/// captionOut 让调用方之后能改标注（比如 hit_rate@k 里的 k 会随 top-k 变）。
inline QWidget *makeMetricTile(const QString &caption, QLabel **valueOut,
                               const QString &initialValue = QStringLiteral("—"),
                               QLabel **captionOut = nullptr,
                               const QString &valueObjectName = QString())
{
    auto *tile = new QWidget;
    auto *layout = new QVBoxLayout(tile);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *value = new QLabel(initialValue);
    value->setObjectName(valueObjectName.isEmpty() ? QStringLiteral("metricValue") : valueObjectName);
    value->setProperty("metric", QStringLiteral("value"));
    auto *label = new QLabel(caption);
    label->setObjectName(QStringLiteral("metricLabel"));
    label->setProperty("metric", QStringLiteral("caption"));
    layout->addWidget(value);
    layout->addWidget(label);
    if (valueOut) {
        *valueOut = value;
    }
    if (captionOut) {
        *captionOut = label;
    }
    return tile;
}

/// 指标卡：参考稿左上那三张（淡彩图标片 + 标题 / 大数字 / 小注解）。
///
/// 和 makeMetricTile() 的分工：makeMetricTile 只是三行文字，要自己找地方放；
/// makeStatCard 自带白卡和图标片，是一张能直接并排摆的完整卡。
/// 页面上"这一屏的核心数字"用它，卡里的次级数字用 makeMetricTile。
struct StatCard
{
    QFrame *card = nullptr;    ///< 整张卡，直接加进布局
    QLabel *value = nullptr;   ///< 大数字
    QLabel *caption = nullptr; ///< 数字下方那行小字（可为空）
};

inline StatCard makeStatCard(const QString &title, Glyph glyph, Tone iconTone,
                             const QString &valueObjectName,
                             const QString &initialValue = QStringLiteral("—"),
                             const QString &initialCaption = QString())
{
    const RagMetrics metrics;
    StatCard parts;

    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(metrics.space4);

    auto *head = new QWidget;
    auto *headLayout = new QHBoxLayout(head);
    headLayout->setContentsMargins(0, 0, 0, 0);
    headLayout->setSpacing(metrics.space3);
    auto *icon = new QLabel;
    icon->setObjectName(QStringLiteral("metricIcon"));
    icon->setProperty("tone", toneName(iconTone));
    icon->setAlignment(Qt::AlignCenter);
    icon->setPixmap(renderGlyph(glyph, QColor(toneColor(iconTone)), 20));
    headLayout->addWidget(icon);
    auto *titleLabel = new QLabel(title);
    titleLabel->setProperty("metric", QStringLiteral("title"));
    headLayout->addWidget(titleLabel, 1);
    layout->addWidget(head);

    auto *block = new QWidget;
    auto *blockLayout = new QVBoxLayout(block);
    blockLayout->setContentsMargins(0, 0, 0, 0);
    blockLayout->setSpacing(2);
    parts.value = new QLabel(initialValue);
    parts.value->setObjectName(valueObjectName.isEmpty() ? QStringLiteral("metricValue")
                                                         : valueObjectName);
    parts.value->setProperty("metric", QStringLiteral("value"));
    parts.caption = new QLabel(initialCaption);
    parts.caption->setProperty("metric", QStringLiteral("caption"));
    blockLayout->addWidget(parts.value);
    blockLayout->addWidget(parts.caption);
    layout->addWidget(block);
    layout->addStretch(1);

    parts.card = makeCard(content);
    return parts;
}

// ---------------------------------------------------------------- 表格

/// 表格统一配置。六张表以前各写各的，行高、省略、滚动模式都不一致。
///
/// 注意：表格现在没有外框也没有网格线，它直接铺在白面板上，本身就是内容而不是容器。
/// 所以不要再包进 makeCard()——那会给它套上一圈本不该有的边。
inline void applyTableDefaults(QTableWidget *table, bool singleSelection = true)
{
    if (!table) {
        return;
    }
    const RagMetrics metrics;
    table->setFrameShape(QFrame::NoFrame);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(singleSelection ? QAbstractItemView::SingleSelection
                                            : QAbstractItemView::ExtendedSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->setWordWrap(false);
    table->setTextElideMode(Qt::ElideRight);
    table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setCornerButtonEnabled(false);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(metrics.rowHeight);
    table->horizontalHeader()->setHighlightSections(false);
    // 表头默认居中，而单元格内容是左对齐的，于是每个表头都浮在它那列文字的右边几十像素，
    // 列越宽错得越远。六张表没有一张调过它——统一改成左对齐，表头才落在它描述的那一列上。
    // （数值列配 makeNumericItem 右对齐，表头需要各页单独右对齐，这里管不到。）
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table->horizontalHeader()->setFixedHeight(metrics.headerHeight);
    table->horizontalHeader()->setMinimumSectionSize(48);
    // 末列拉伸会和「按需隐藏最后一列」打架，各页自己决定列宽策略。
    table->horizontalHeader()->setStretchLastSection(false);
}

/// 表格空态提示。改版前空表就是一片白，用户分不清「没有结果」和「还没查询」。
///
/// 对齐方式要选：表格很高时居中会把提示扔到一大片空白的正中间，那正是"大片留白里
/// 浮着一行字"的观感来源；顶部对齐让提示紧跟表头，读起来像表格自己的说明。
/// 行高不高的表用居中，占满半屏的表用 Qt::AlignTop。
///
/// 用法：
///     auto *empty = new ragui::EmptyStateOverlay(table, QStringLiteral("尚无证据"));
///     empty->setVisible(table->rowCount() == 0);
class EmptyStateOverlay : public QLabel
{
public:
    EmptyStateOverlay(QAbstractScrollArea *view, const QString &text,
                      Qt::Alignment alignment = Qt::AlignCenter)
        : QLabel(view ? view->viewport() : nullptr)
    {
        const RagMetrics metrics;
        setObjectName(QStringLiteral("emptyHint"));
        setAlignment(alignment);
        setWordWrap(true);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        // 顶部对齐时留出一行的呼吸，否则提示会贴在表头下沿。
        setContentsMargins(metrics.space5, (alignment & Qt::AlignTop) ? metrics.space5 : 0,
                           metrics.space5, 0);
        setText(text);
        if (view && view->viewport()) {
            viewport_ = view->viewport();
            viewport_->installEventFilter(this);
            setGeometry(viewport_->rect());
            raise();
        }
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == viewport_ && event->type() == QEvent::Resize) {
            setGeometry(viewport_->rect());
        }
        return QLabel::eventFilter(watched, event);
    }

private:
    QWidget *viewport_ = nullptr;
};

/// 表格单元格：右对齐等宽数字（分数、耗时、chunk 数）。
inline QTableWidgetItem *makeNumericItem(const QString &text, const QString &tooltip = QString())
{
    auto *item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QFont font = item->font();
    font.setFamily(QStringLiteral("Consolas"));
    item->setFont(font);
    item->setToolTip(tooltip.isEmpty() ? text : tooltip);
    return item;
}

/// 表格单元格：普通文本，完整内容自动进 tooltip（长路径、章节名不会看不全）。
inline QTableWidgetItem *makeTextItem(const QString &text, const QString &tooltip = QString())
{
    auto *item = new QTableWidgetItem(text);
    item->setToolTip(tooltip.isEmpty() ? text : tooltip);
    return item;
}

/// 表格单元格：带语义色的状态文字（缺失 / 待重建 / 错误）。
/// 表格单元格是 QTableWidgetItem 不是 QWidget，QSS 管不到，所以颜色只能从 token 里取。
inline QTableWidgetItem *makeStatusItem(const QString &text, Tone tone,
                                        const QString &tooltip = QString())
{
    auto *item = new QTableWidgetItem(text);
    item->setForeground(QBrush(QColor(toneColor(tone))));
    if (tone == Tone::Warning || tone == Tone::Error) {
        QFont font = item->font();
        font.setBold(true);
        item->setFont(font);
    }
    item->setToolTip(tooltip.isEmpty() ? text : tooltip);
    return item;
}

// ---------------------------------------------------------------- 浮层基底

/// 窗体的"桌面"：一片薄荷底 + 浮在上面的白色圆角面板。
///
/// 这是整套视觉的第一层，也是唯一一次明确的色彩落差。面板内部之所以能几乎不用边框，
/// 就是因为"白 vs 薄荷"已经把内容区和窗体分开了；如果这一层做成纯白，
/// 里面每个卡片就必须自己长边框才立得住，一屏就会退化成一堆方框。
///
/// 投影是自绘的：QSS 没有 box-shadow，QGraphicsDropShadowEffect 又会给整棵子树
/// 强制光栅缓存，和表格/滚动区一起用会出重绘残影。这里用同心圆角矩形按二次衰减叠加，
/// 十八圈叠出一条软边，成本可控且完全可离屏验收。
///
/// 用法（布局边距必须等于 inset()，否则内容会压到薄荷底上）：
///     auto *central = new ragui::SurfaceBackground;
///     auto *layout = new QHBoxLayout(central);
///     layout->setContentsMargins(central->inset(), ...);
class SurfaceBackground : public QWidget
{
public:
    explicit SurfaceBackground(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAutoFillBackground(false);
    }

    /// 白面板相对窗体的内缩量。布局边距照抄它。
    static int inset() { return kInset; }
    static int radius() { return kRadius; }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        cache_ = QPixmap(); // 尺寸变了就重画，其余时候直接贴图
        QWidget::resizeEvent(event);
    }

    void paintEvent(QPaintEvent *) override
    {
        const qreal dpr = devicePixelRatioF();
        if (cache_.isNull() || cache_.size() != size() * dpr) {
            rebuildCache(dpr);
        }
        QPainter painter(this);
        painter.drawPixmap(0, 0, cache_);
    }

private:
    void rebuildCache(qreal dpr)
    {
        const RagPalette palette;
        cache_ = QPixmap(size() * dpr);
        cache_.setDevicePixelRatio(dpr);
        cache_.fill(Qt::transparent);

        QPainter painter(&cache_);
        painter.setRenderHint(QPainter::Antialiasing, true);

        // 1) 薄荷底。极弱的对角渐变——大面积纯色会显得像还没渲染完。
        QLinearGradient ground(0, 0, width() * 0.6, height());
        ground.setColorAt(0.0, QColor(palette.ground));
        ground.setColorAt(1.0, QColor(palette.groundDeep));
        painter.fillRect(rect(), ground);

        const QRectF surface =
            QRectF(rect()).adjusted(kInset, kInset, -kInset, -kInset);
        if (surface.width() <= 0 || surface.height() <= 0) {
            return;
        }

        // 2) 投影。外圈几乎全透明，越往内越实；同一个像素被所有比它更外的圈覆盖，
        //    叠起来正好是一条平滑的软边。竖直方向多给一点，光从上方来。
        //    颜色取底色的深绿相而不是纯黑——纯黑压在彩色底上会发灰、显脏。
        painter.setPen(Qt::NoPen);
        for (int ring = kShadowSpread; ring >= 1; --ring) {
            const qreal t = qreal(ring) / kShadowSpread;
            QColor shade(palette.groundShadow);
            shade.setAlphaF(0.030 * (1.0 - t) * (1.0 - t));
            painter.setBrush(shade);
            const QRectF halo =
                surface.adjusted(-ring, -ring * 0.5, ring, ring * 1.4);
            painter.drawRoundedRect(halo, kRadius + ring, kRadius + ring);
        }

        // 3) 白面板。
        painter.setBrush(QColor(palette.surface));
        painter.drawRoundedRect(surface, kRadius, kRadius);
    }

    static constexpr int kInset = 16;
    static constexpr int kRadius = 24;
    static constexpr int kShadowSpread = 18;

    QPixmap cache_;
};

} // namespace ragui

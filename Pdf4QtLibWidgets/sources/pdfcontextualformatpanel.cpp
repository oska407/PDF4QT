// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "pdfcontextualformatpanel.h"

#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfwidgetannotation.h"
#include "pdfwidgettool.h"
#include "pdfpagecontentelements.h"
#include "pdfpagecontenteditortools.h"
#include "pdfadvancedtools.h"
#include "pdfannotation.h"
#include "pdfadvancedformatdialog.h"
#include "pdfwidgetutils.h"

#include <QStackedWidget>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QToolButton>
#include <QButtonGroup>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QLabel>
#include <QTimer>
#include <QSignalBlocker>
#include <QPainter>
#include <QPixmap>
#include <QIcon>
#include <QFontMetrics>
#include <QList>
#include <QStringList>
#include <QPoint>
#include <QSize>
#include <QEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QStyle>
#include <QtMath>

#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <vector>

/// Grey preview box with the "Sample" text and a highlight bar, whose color and
/// alpha follow the annotation color / opacity currently shown in the panel.
class FormatPreviewWidget : public QWidget
{
public:
    explicit FormatPreviewWidget(QWidget* parent = nullptr) :
        QWidget(parent),
        m_color(Qt::yellow),
        m_opacity(100)
    {
        setObjectName(QStringLiteral("formatPanelPreview"));
        setMinimumHeight(QFontMetrics(font()).height() * 4);
        setAutoFillBackground(true);
    }

    void setValues(const QColor& color, int opacity)
    {
        m_color = color;
        m_opacity = opacity;
        update();
    }

protected:
    virtual void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.fillRect(rect(), QColor(128, 128, 128));

        QColor highlight = m_color.isValid() ? m_color : QColor(Qt::yellow);
        highlight.setAlphaF(qBound(0.0, static_cast<double>(m_opacity) / 100.0, 1.0));

        const int verticalMargin = height() / 3;
        const QRect highlightRect = rect().adjusted(8, verticalMargin, -8, -verticalMargin);
        painter.fillRect(highlightRect, highlight);

        painter.setPen(QColor(Qt::black));
        painter.drawText(rect(), Qt::AlignCenter, QObject::tr("Sample"));
    }

private:
    QColor m_color;
    int m_opacity;
};

/// RAII helper which blocks the signals of a group of controls for the whole
/// duration of a model -> panel update. QSignalBlocker restores the previous
/// state of every control in its destructor.
class ControlSignalBlockers
{
public:
    explicit ControlSignalBlockers(const QList<QObject*>& controls)
    {
        m_blockers.reserve(static_cast<size_t>(controls.size()));
        for (QObject* control : controls)
        {
            if (control)
            {
                m_blockers.push_back(std::make_unique<QSignalBlocker>(control));
            }
        }
    }

private:
    std::vector<std::unique_ptr<QSignalBlocker>> m_blockers;
};

namespace
{

QIcon createColorIcon(const QColor& color, int size)
{
    QPixmap pixmap(size, size);
    if (color.isValid())
    {
        pixmap.fill(color);
    }
    else
    {
        pixmap.fill(Qt::transparent);
    }
    return QIcon(pixmap);
}

}   // namespace

namespace pdf
{

PDFContextualFormatPanel::PDFContextualFormatPanel(PDFWidget* widget, QWidget* parent) :
    QDockWidget(parent),
    m_widget(widget),
    m_annotationManager(widget ? widget->getAnnotationManager() : nullptr),
    m_scene(nullptr),
    m_freeTextTool(nullptr),
    m_lastActiveTool(nullptr),
    m_stack(new QStackedWidget(this)),
    m_annotationCard(nullptr),
    m_textCard(nullptr),
    m_emptyCard(nullptr),
    m_inlinePalette(nullptr),
    m_preview(nullptr),
    m_toolPollTimer(nullptr),
    m_card(Card::Empty),
    m_isUpdatingFromModel(false),
    m_stateUpdatePending(false),
    m_lastFreeTextEditingState(false),
    m_inlineEditInTransaction(false),
    m_freeTextDirty(false),
    m_freeTextAnchorSignature(),
    m_colorButtonGroup(nullptr),
    m_opacitySlider(nullptr),
    m_opacitySpinBox(nullptr),
    m_mixedHintLabel(nullptr),
    m_freeTextEdit(nullptr),
    m_freeTextCommitTimer(nullptr),
    m_annotationColor(Qt::yellow),
    m_annotationOpacity(100),
    m_annotationColorMixed(false),
    m_annotationOpacityMixed(false),
    m_fontCombo(nullptr),
    m_fontSizeSpinBox(nullptr),
    m_textColorButton(nullptr),
    m_boldButton(nullptr),
    m_italicButton(nullptr),
    m_underlineButton(nullptr),
    m_strikeOutButton(nullptr),
    m_annotationSetAsDefaultButton(nullptr),
    m_textSetAsDefaultButton(nullptr),
    m_lockedCheckBox(nullptr),
    m_textAlignmentGroup(nullptr),
    m_inlineTextEdit(nullptr),
    m_textColor(Qt::black),
    m_textAlignment(Qt::AlignLeft)
{
    setObjectName(QStringLiteral("ContextualFormatPanel"));
    setWindowTitle(tr("Format"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetClosable |
                QDockWidget::DockWidgetMovable |
                QDockWidget::DockWidgetFloatable);

    m_annotationCard = createAnnotationCard();
    m_textCard = createTextCard();
    m_emptyCard = createEmptyCard();

    m_stack->addWidget(m_annotationCard);
    m_stack->addWidget(m_textCard);
    m_stack->addWidget(m_emptyCard);
    m_stack->setCurrentWidget(m_emptyCard);

    QWidget* root = new QWidget(this);
    QVBoxLayout* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(4, 4, 4, 4);
    rootLayout->setSpacing(4);
    rootLayout->addWidget(m_stack);
    rootLayout->addStretch(1);
    setWidget(root);

    PDFWidgetUtils::style(this);

    if (m_annotationManager)
    {
        connect(m_annotationManager, &PDFWidgetAnnotationManager::selectionChanged,
                this, &PDFContextualFormatPanel::onAnnotationSelectionChanged);
    }

    if (m_widget)
    {
        connect(m_widget, &PDFWidget::sceneActivityChanged,
                this, &PDFContextualFormatPanel::onActiveToolChanged);
    }

    m_toolPollTimer = new QTimer(this);
    m_toolPollTimer->setInterval(250);
    connect(m_toolPollTimer, &QTimer::timeout,
            this, &PDFContextualFormatPanel::onToolPollTimeout);

    // Rebuilding the appearance stream of an annotation is much more expensive
    // than replacing a page content element, so typing into the free text
    // editor is written back once after the user stops typing.
    m_freeTextCommitTimer = new QTimer(this);
    m_freeTextCommitTimer->setSingleShot(true);
    m_freeTextCommitTimer->setInterval(400);
    connect(m_freeTextCommitTimer, &QTimer::timeout,
            this, &PDFContextualFormatPanel::commitFreeTextContents);

    m_inlinePalette = createInlineColorPalette();

    scheduleStateUpdate();
}

PDFContextualFormatPanel::~PDFContextualFormatPanel()
{
    // A destroyed panel must never leave the undo transaction of the editor
    // plugin open.
    finishInlineEditTransaction();

    // Do not lose the text typed in the last few hundred milliseconds. The
    // call is unconditional, the dirty / anchor guards inside make it a no-op
    // when nothing was typed and idempotent otherwise.
    commitFreeTextContents();

    if (m_scene)
    {
        unregisterPageContentScene(m_scene);
    }

    // The inline palette is a top level Qt::Popup window, so it is not owned by
    // the Qt object tree of this dock widget.
    delete m_inlinePalette;
    m_inlinePalette = nullptr;
}

QToolButton* PDFContextualFormatPanel::createDisabledPlaceholder(const QString& text)
{
    QToolButton* button = new QToolButton(this);
    button->setText(text);
    button->setEnabled(false);
    button->setToolTip(tr("当前版本不支持"));
    button->setObjectName(QStringLiteral("formatPanelDisabledPlaceholder"));
    return button;
}

QToolButton* PDFContextualFormatPanel::createColorSwatchButton(const QColor& color,
                                                              const QString& toolTip,
                                                              bool enabled)
{
    const int swatchSize = QFontMetrics(font()).height() + 6;

    QToolButton* button = new QToolButton(this);
    button->setObjectName(QStringLiteral("formatPanelColorSwatch"));
    button->setToolTip(toolTip);
    button->setCheckable(true);
    button->setEnabled(enabled);
    button->setIconSize(QSize(swatchSize, swatchSize));
    button->setIcon(createColorIcon(color, swatchSize));
    return button;
}

QWidget* PDFContextualFormatPanel::createCollapsibleGroup(const QString& title,
                                                         const QString& key,
                                                         QWidget* content,
                                                         bool defaultExpanded)
{
    QWidget* container = new QWidget(this);
    container->setObjectName(QStringLiteral("formatPanelGroup_%1").arg(key));

    QVBoxLayout* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    QToolButton* toggle = new QToolButton(container);
    toggle->setObjectName(QStringLiteral("formatPanelGroupToggle_%1").arg(key));
    toggle->setText(title);
    toggle->setCheckable(true);
    toggle->setAutoRaise(true);
    toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    const bool expanded = m_groupExpanded.value(key, defaultExpanded);
    toggle->setChecked(expanded);
    toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    content->setVisible(expanded);

    layout->addWidget(toggle);
    layout->addWidget(content);

    connect(toggle, &QToolButton::toggled, this,
            [this, key, content, toggle](bool checked)
    {
        m_groupExpanded.insert(key, checked);
        content->setVisible(checked);
        toggle->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
    });

    return container;
}

QWidget* PDFContextualFormatPanel::createInlineColorPalette()
{
    QWidget* palette = new QWidget(nullptr, Qt::Popup);
    palette->setObjectName(QStringLiteral("formatPanelInlinePalette"));
    palette->setWindowTitle(tr("Text color"));

    QGridLayout* grid = new QGridLayout(palette);
    grid->setContentsMargins(4, 4, 4, 4);
    grid->setSpacing(2);

    const QList<QColor> colors =
    {
        QColor(Qt::yellow), QColor(Qt::cyan), QColor(Qt::green),
        QColor(255, 192, 203), QColor(255, 127, 80), QColor(Qt::darkGreen),
        QColor(Qt::blue), QColor(Qt::black), QColor(Qt::gray),
        QColor(Qt::white), QColor(Qt::magenta), QColor(Qt::darkRed)
    };

    const int swatchSize = QFontMetrics(font()).height() + 6;
    for (int i = 0; i < colors.size(); ++i)
    {
        const QColor color = colors.at(i);
        QToolButton* button = new QToolButton(palette);
        button->setObjectName(QStringLiteral("formatPanelInlineColor"));
        button->setIconSize(QSize(swatchSize, swatchSize));
        button->setIcon(createColorIcon(color, swatchSize));
        button->setToolTip(color.name(QColor::HexRgb));
        grid->addWidget(button, i / 4, i % 4);

        connect(button, &QToolButton::clicked, this,
                [this, palette, color]()
        {
            palette->hide();
            onInlineColorSelected(color);
        });
    }

    PDFWidgetUtils::style(palette);
    return palette;
}

QWidget* PDFContextualFormatPanel::createArrangeGroup(QWidget* parent,
                                                      ArrangeControls& controls,
                                                      bool elementSide)
{
    controls.elementSide = elementSide;

    QWidget* content = new QWidget(parent);
    QVBoxLayout* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(3);

    // --- Page Center -------------------------------------------------------
    controls.pageCenter = new QToolButton(content);
    controls.pageCenter->setObjectName(QStringLiteral("formatPanelPageCenterButton"));
    controls.pageCenter->setText(tr("Page Center"));
    controls.pageCenter->setToolTip(tr("将选中对象移到页面中心（逐页处理，一笔撤销）"));
    controls.pageCenter->setEnabled(false);
    layout->addWidget(controls.pageCenter);
    connect(controls.pageCenter, &QToolButton::clicked, this,
            [this, elementSide]() { onArrangePageCenter(elementSide); });

    // --- Rotate ------------------------------------------------------------
    layout->addWidget(new QLabel(tr("Rotate"), content));

    QWidget* rotateRow = new QWidget(content);
    QHBoxLayout* rotateLayout = new QHBoxLayout(rotateRow);
    rotateLayout->setContentsMargins(0, 0, 0, 0);

    controls.rotateClockwise = new QToolButton(rotateRow);
    controls.rotateClockwise->setObjectName(QStringLiteral("formatPanelRotateClockwiseButton"));
    controls.rotateClockwise->setText(tr("↻ 90°"));
    controls.rotateClockwise->setToolTip(tr("顺时针旋转 90°"));
    controls.rotateClockwise->setEnabled(false);

    controls.rotateCounterClockwise = new QToolButton(rotateRow);
    controls.rotateCounterClockwise->setObjectName(QStringLiteral("formatPanelRotateCounterClockwiseButton"));
    controls.rotateCounterClockwise->setText(tr("↺ 90°"));
    controls.rotateCounterClockwise->setToolTip(tr("逆时针旋转 90°"));
    controls.rotateCounterClockwise->setEnabled(false);

    controls.rotateAngle = new QDoubleSpinBox(rotateRow);
    controls.rotateAngle->setObjectName(QStringLiteral("formatPanelRotateAngleSpinBox"));
    controls.rotateAngle->setRange(-180.0, 180.0);
    controls.rotateAngle->setDecimals(1);
    controls.rotateAngle->setSingleStep(1.0);
    controls.rotateAngle->setSuffix(tr("°"));
    controls.rotateAngle->setValue(0.0);
    // Without keyboard tracking the value is emitted once, when the user really
    // finished the input (Enter / focus out / step button), so "4" and "5" typed
    // in a row cannot be applied as 4° + 45°.
    controls.rotateAngle->setKeyboardTracking(false);
    controls.rotateAngle->setToolTip(tr("旋转增量(°)：回车后按增量旋转一次，然后自动归零"));
    controls.rotateAngle->setEnabled(false);

    rotateLayout->addWidget(controls.rotateClockwise);
    rotateLayout->addWidget(controls.rotateCounterClockwise);
    layout->addWidget(rotateRow);

    // The relative angle is an increment only (AC-16), the label says so.
    QWidget* rotateAngleRow = new QWidget(content);
    QHBoxLayout* rotateAngleLayout = new QHBoxLayout(rotateAngleRow);
    rotateAngleLayout->setContentsMargins(0, 0, 0, 0);
    rotateAngleLayout->addWidget(new QLabel(tr("旋转增量(°)"), rotateAngleRow));
    rotateAngleLayout->addWidget(controls.rotateAngle, 1);
    layout->addWidget(rotateAngleRow);

    connect(controls.rotateClockwise, &QToolButton::clicked, this,
            [this, elementSide]() { onArrangeRotate(elementSide, 1); });
    connect(controls.rotateCounterClockwise, &QToolButton::clicked, this,
            [this, elementSide]() { onArrangeRotate(elementSide, -1); });
    connect(controls.rotateAngle, &QDoubleSpinBox::valueChanged, this,
            [this, elementSide](double value) { onArrangeRotateAngle(elementSide, value); });

    // --- Flip --------------------------------------------------------------
    layout->addWidget(new QLabel(tr("Flip"), content));

    QWidget* flipRow = new QWidget(content);
    QHBoxLayout* flipLayout = new QHBoxLayout(flipRow);
    flipLayout->setContentsMargins(0, 0, 0, 0);

    controls.flipHorizontal = new QToolButton(flipRow);
    controls.flipHorizontal->setObjectName(QStringLiteral("formatPanelFlipHorizontalButton"));
    controls.flipHorizontal->setText(tr("水平翻转"));
    controls.flipHorizontal->setToolTip(tr("左右镜像"));
    controls.flipHorizontal->setEnabled(false);

    controls.flipVertical = new QToolButton(flipRow);
    controls.flipVertical->setObjectName(QStringLiteral("formatPanelFlipVerticalButton"));
    controls.flipVertical->setText(tr("垂直翻转"));
    controls.flipVertical->setToolTip(tr("上下镜像"));
    controls.flipVertical->setEnabled(false);

    flipLayout->addWidget(controls.flipHorizontal);
    flipLayout->addWidget(controls.flipVertical);
    layout->addWidget(flipRow);

    connect(controls.flipHorizontal, &QToolButton::clicked, this,
            [this, elementSide]() { onArrangeFlip(elementSide, Qt::Horizontal); });
    connect(controls.flipVertical, &QToolButton::clicked, this,
            [this, elementSide]() { onArrangeFlip(elementSide, Qt::Vertical); });

    // --- Alignment ---------------------------------------------------------
    layout->addWidget(new QLabel(tr("Alignment"), content));

    QWidget* alignmentRow = new QWidget(content);
    QHBoxLayout* alignmentLayout = new QHBoxLayout(alignmentRow);
    alignmentLayout->setContentsMargins(0, 0, 0, 0);

    const QStringList alignmentNames =
    {
        tr("左对齐"), tr("水平居中"), tr("右对齐"),
        tr("上对齐"), tr("垂直居中"), tr("下对齐")
    };
    // G-15: page content elements have no anchor, they are aligned to the frame
    // of the selection. The tooltip of the element side must say so.
    const QString alignmentTip = elementSide ? tr("对象对齐：按选择框对齐")
                                             : tr("对象对齐：以最后选中的对象为锚点");
    for (int i = 0; i < alignmentNames.size(); ++i)
    {
        QToolButton* button = new QToolButton(alignmentRow);
        button->setObjectName(QStringLiteral("formatPanelAlignButton"));
        button->setText(alignmentNames.at(i));
        button->setToolTip(alignmentTip);
        button->setEnabled(false);
        alignmentLayout->addWidget(button);
        controls.alignButtons.append(button);
        connect(button, &QToolButton::clicked, this,
                [this, i, elementSide]() { onArrangeAlignment(i, elementSide); });
    }
    layout->addWidget(alignmentRow);

    // --- Distribute --------------------------------------------------------
    layout->addWidget(new QLabel(tr("Distribute"), content));

    QWidget* distributeRow = new QWidget(content);
    QHBoxLayout* distributeLayout = new QHBoxLayout(distributeRow);
    distributeLayout->setContentsMargins(0, 0, 0, 0);

    const QStringList distributeNames = { tr("水平分布"), tr("垂直分布") };
    for (int i = 0; i < distributeNames.size(); ++i)
    {
        QToolButton* button = new QToolButton(distributeRow);
        button->setObjectName(QStringLiteral("formatPanelDistributeButton"));
        button->setText(distributeNames.at(i));
        button->setToolTip(elementSide ? tr("当前版本不支持")
                                       : tr("等间距分布（至少选中 3 个对象）"));
        button->setEnabled(false);
        distributeLayout->addWidget(button);
        controls.distributeButtons.append(button);
        connect(button, &QToolButton::clicked, this,
                [this, i, elementSide]() { onArrangeDistribute(i, elementSide); });
    }
    layout->addWidget(distributeRow);

    return content;
}

QWidget* PDFContextualFormatPanel::createCardFooterRow(QToolButton** defaultButton)
{
    QWidget* row = new QWidget(this);
    QHBoxLayout* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);

    // R-20: the button is deliberately explicit. Unlike Foxit, changing the
    // properties of a selected object never rewrites the tool defaults, so the
    // user needs one visible action that promotes the current look.
    QToolButton* button = new QToolButton(row);
    button->setObjectName(QStringLiteral("formatPanelSetAsDefault"));
    button->setText(tr("设为默认"));
    button->setToolTip(tr("把当前样式写入后续新建对象的默认样式（选中的对象本身不改变）"));
    button->setAutoRaise(true);
    button->setEnabled(false);
    rowLayout->addWidget(button);
    rowLayout->addStretch(1);

    connect(button, &QToolButton::clicked,
            this, &PDFContextualFormatPanel::onSetAsDefaultClicked);

    // R-21: the advanced entry is deliberately weak and sits at the trailing edge
    // of the footer, so it never competes with the primary controls. It opens a
    // modeless dialog and is not the only way to reach any P0 property (AC-15).
    QToolButton* advancedButton = new QToolButton(row);
    advancedButton->setObjectName(QStringLiteral("formatPanelAdvanced"));
    advancedButton->setText(tr("高级…"));
    advancedButton->setAutoRaise(true);
    advancedButton->setToolTip(tr("边框线宽 / 作者 / 精确缩放（非模态窗口，不阻塞页面）"));
    rowLayout->addWidget(advancedButton);

    connect(advancedButton, &QToolButton::clicked,
            this, &PDFContextualFormatPanel::openAdvancedDialog);

    if (defaultButton)
    {
        *defaultButton = button;
    }

    return row;
}

QWidget* PDFContextualFormatPanel::createAnnotationCard()
{
    QWidget* card = new QWidget(this);
    card->setObjectName(QStringLiteral("formatPanelAnnotationCard"));
    QVBoxLayout* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(4);

    // --- Preview -----------------------------------------------------------
    m_preview = new FormatPreviewWidget(card);
    QWidget* previewContent = new QWidget(card);
    QVBoxLayout* previewLayout = new QVBoxLayout(previewContent);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->addWidget(m_preview);
    cardLayout->addWidget(createCollapsibleGroup(tr("Preview"),
                                                QStringLiteral("preview"),
                                                previewContent, true));

    // --- Text (free text annotations only) ---------------------------------
    QWidget* freeTextContent = new QWidget(card);
    QVBoxLayout* freeTextLayout = new QVBoxLayout(freeTextContent);
    freeTextLayout->setContentsMargins(0, 0, 0, 0);

    QLabel* freeTextLabel = new QLabel(tr("注释文字（即时生效）"), freeTextContent);
    freeTextLabel->setWordWrap(true);
    freeTextLayout->addWidget(freeTextLabel);

    m_freeTextEdit = new QPlainTextEdit(freeTextContent);
    m_freeTextEdit->setObjectName(QStringLiteral("formatPanelFreeText"));
    m_freeTextEdit->setPlaceholderText(tr("混合 (Mixed)"));
    m_freeTextEdit->setTabChangesFocus(true);
    m_freeTextEdit->setMinimumHeight(QFontMetrics(font()).lineSpacing() * 4);
    m_freeTextEdit->setEnabled(false);
    m_freeTextEdit->setToolTip(tr("仅自由文本注释支持编辑文字"));
    m_freeTextEdit->installEventFilter(this);
    freeTextLayout->addWidget(m_freeTextEdit);

    connect(m_freeTextEdit, &QPlainTextEdit::textChanged,
            this, &PDFContextualFormatPanel::onFreeTextEdited);

    cardLayout->addWidget(createCollapsibleGroup(tr("Text"),
                                                QStringLiteral("annotationText"),
                                                freeTextContent, true));

    // --- Highlight Style ---------------------------------------------------
    QWidget* styleContent = new QWidget(card);
    QVBoxLayout* styleLayout = new QVBoxLayout(styleContent);
    styleLayout->setContentsMargins(0, 0, 0, 0);
    styleLayout->setSpacing(2);

    QLabel* styleLabel = new QLabel(tr("Highlight Style"), styleContent);
    styleLayout->addWidget(styleLabel);

    QWidget* swatchRow = new QWidget(styleContent);
    QGridLayout* swatchLayout = new QGridLayout(swatchRow);
    swatchLayout->setContentsMargins(0, 0, 0, 0);
    swatchLayout->setSpacing(2);

    struct Swatch
    {
        QColor color;
        QString name;
        bool enabled;
    };

    const QList<Swatch> swatches =
    {
        { QColor(255, 255, 0),   tr("黄色"),       true  },
        { QColor(0, 255, 255),   tr("青色"),       true  },
        { QColor(0, 255, 0),     tr("绿色"),       true  },
        { QColor(255, 192, 203), tr("粉色"),       true  },
        { QColor(255, 127, 80),  tr("珊瑚红"),     true  },
        { QColor(),              tr("多色环"),     false },
        { QColor(0, 128, 0),     tr("深绿"),       true  },
        { QColor(0, 0, 255),     tr("蓝色"),       true  },
        { QColor(0, 0, 0),       tr("黑色"),       true  },
        { QColor(128, 128, 128), tr("灰色"),       true  },
        { QColor(255, 255, 255), tr("白色"),       true  },
        { QColor(),              tr("吸管取色"),   false }
    };

    m_colorButtonGroup = new QButtonGroup(this);
    m_colorButtonGroup->setExclusive(true);

    for (int i = 0; i < swatches.size(); ++i)
    {
        const Swatch& swatch = swatches.at(i);
        QToolButton* button = createColorSwatchButton(swatch.color, swatch.name, swatch.enabled);
        swatchLayout->addWidget(button, i / 6, i % 6);
        m_colorButtons.append(button);
        m_colorPalette.append(swatch.color);
        m_colorButtonGroup->addButton(button, i);

        connect(button, &QToolButton::clicked, this,
                [this, i]() { onColorButtonClicked(i); });
    }

    styleLayout->addWidget(swatchRow);

    m_mixedHintLabel = new QLabel(styleContent);
    m_mixedHintLabel->setObjectName(QStringLiteral("formatPanelMixedHint"));
    m_mixedHintLabel->clear();
    styleLayout->addWidget(m_mixedHintLabel);

    cardLayout->addWidget(createCollapsibleGroup(tr("Highlight Style"),
                                                QStringLiteral("highlightStyle"),
                                                styleContent, true));

    // --- Opacity -----------------------------------------------------------
    QWidget* opacityContent = new QWidget(card);
    QHBoxLayout* opacityLayout = new QHBoxLayout(opacityContent);
    opacityLayout->setContentsMargins(0, 0, 0, 0);

    QLabel* minimumLabel = new QLabel(tr("0"), opacityContent);
    m_opacitySlider = new QSlider(Qt::Horizontal, opacityContent);
    m_opacitySlider->setObjectName(QStringLiteral("formatPanelOpacitySlider"));
    m_opacitySlider->setRange(0, 100);
    m_opacitySlider->setValue(100);
    m_opacitySlider->setTracking(true);

    m_opacitySpinBox = new QSpinBox(opacityContent);
    m_opacitySpinBox->setObjectName(QStringLiteral("formatPanelOpacitySpinBox"));
    m_opacitySpinBox->setRange(0, 100);
    m_opacitySpinBox->setValue(100);
    m_opacitySpinBox->setSuffix(tr("%"));

    QLabel* maximumLabel = new QLabel(tr("100"), opacityContent);

    opacityLayout->addWidget(minimumLabel);
    opacityLayout->addWidget(m_opacitySlider, 1);
    opacityLayout->addWidget(m_opacitySpinBox);
    opacityLayout->addWidget(maximumLabel);

    connect(m_opacitySlider, &QSlider::valueChanged,
            this, &PDFContextualFormatPanel::onOpacityChanged);
    connect(m_opacitySpinBox, &QSpinBox::valueChanged,
            this, &PDFContextualFormatPanel::onOpacitySpinBoxChanged);
    // No continuousEditStarted / continuousEditFinished is emitted here. The
    // annotation write back goes through
    // PDFWidgetAnnotationManager::documentModified ->
    // PDFProgramController::onDocumentModified ->
    // PDFUndoRedoManager::createUndo(), so every write back is one undo step
    // and PDFUndoRedoManager has no begin / end transaction API to merge them.
    // Merging the drag of the opacity slider is therefore NOT implemented in
    // this version (known limitation KI-01). Emitting the two signals here
    // would only clear the local (page content) redo stack of the plugin.

    cardLayout->addWidget(createCollapsibleGroup(tr("Opacity"),
                                                QStringLiteral("opacity"),
                                                opacityContent, true));

    // --- Arrange -----------------------------------------------------------
    // R-16: Page Center / Rotate (+-90 / relative angle) / Flip / Alignment /
    // Distribute. The same group (other target side) is added to the text card,
    // see createArrangeGroup() and createTextCard().
    cardLayout->addWidget(createCollapsibleGroup(tr("Arrange"),
                                                QStringLiteral("arrange"),
                                                createArrangeGroup(card, m_annotationArrange, false),
                                                true));
    // --- Lock (R-22, P2) ----------------------------------------------------
    // The checkbox mirrors the /F Locked flag. While the flag is set, the
    // existing canModifyAnnotation() / canTransformAnnotation() filters reject
    // every drag / recolor / delete for free; the unlock direction goes through
    // the dedicated manager path (see setSelectedAnnotationsLocked()).
    m_lockedCheckBox = new QCheckBox(tr("锁定"), card);
    m_lockedCheckBox->setObjectName(QStringLiteral("formatPanelLocked"));
    m_lockedCheckBox->setTristate(true);
    m_lockedCheckBox->setToolTip(tr("锁定后标注不可拖动/修改/删除（再次取消勾选可解锁）"));
    cardLayout->addWidget(m_lockedCheckBox);

    connect(m_lockedCheckBox, &QCheckBox::toggled,
            this, &PDFContextualFormatPanel::onLockedToggled);

    cardLayout->addWidget(createCardFooterRow(&m_annotationSetAsDefaultButton));
    cardLayout->addStretch(1);
    return card;
}

QWidget* PDFContextualFormatPanel::createTextCard()
{
    QWidget* card = new QWidget(this);
    card->setObjectName(QStringLiteral("formatPanelTextCard"));
    QVBoxLayout* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(4);

    QWidget* styleContent = new QWidget(card);
    QVBoxLayout* styleLayout = new QVBoxLayout(styleContent);
    styleLayout->setContentsMargins(0, 0, 0, 0);
    styleLayout->setSpacing(3);

    // Font family, size and color
    QWidget* fontRow = new QWidget(styleContent);
    QHBoxLayout* fontLayout = new QHBoxLayout(fontRow);
    fontLayout->setContentsMargins(0, 0, 0, 0);

    m_fontCombo = new QFontComboBox(fontRow);
    m_fontCombo->setObjectName(QStringLiteral("formatPanelFontCombo"));

    m_fontSizeSpinBox = new QDoubleSpinBox(fontRow);
    m_fontSizeSpinBox->setObjectName(QStringLiteral("formatPanelFontSize"));
    m_fontSizeSpinBox->setRange(1.0, 512.0);
    m_fontSizeSpinBox->setDecimals(2);
    m_fontSizeSpinBox->setValue(10.0);

    m_textColorButton = new QToolButton(fontRow);
    m_textColorButton->setObjectName(QStringLiteral("formatPanelTextColor"));
    m_textColorButton->setToolTip(tr("文字颜色"));
    m_textColorButton->setIcon(createColorIcon(m_textColor, QFontMetrics(font()).height() + 6));
    m_textColorButton->setIconSize(QSize(QFontMetrics(font()).height() + 6,
                                         QFontMetrics(font()).height() + 6));

    fontLayout->addWidget(m_fontCombo, 1);
    fontLayout->addWidget(m_fontSizeSpinBox);
    fontLayout->addWidget(m_textColorButton);
    styleLayout->addWidget(fontRow);

    connect(m_fontCombo, &QFontComboBox::currentFontChanged,
            this, &PDFContextualFormatPanel::onFontFamilyChanged);
    connect(m_fontSizeSpinBox, &QDoubleSpinBox::valueChanged,
            this, &PDFContextualFormatPanel::onFontSizeChanged);
    connect(m_textColorButton, &QToolButton::clicked,
            this, &PDFContextualFormatPanel::onTextColorButtonClicked);

    // B / I / U and the disabled P2 placeholders
    QWidget* decorationRow = new QWidget(styleContent);
    QHBoxLayout* decorationLayout = new QHBoxLayout(decorationRow);
    decorationLayout->setContentsMargins(0, 0, 0, 0);

    m_boldButton = new QToolButton(decorationRow);
    m_boldButton->setText(tr("B"));
    m_boldButton->setCheckable(true);
    m_italicButton = new QToolButton(decorationRow);
    m_italicButton->setText(tr("I"));
    m_italicButton->setCheckable(true);
    m_underlineButton = new QToolButton(decorationRow);
    m_underlineButton->setText(tr("U"));
    m_underlineButton->setCheckable(true);

    decorationLayout->addWidget(m_boldButton);
    decorationLayout->addWidget(m_italicButton);
    decorationLayout->addWidget(m_underlineButton);
    // Strikeout is supported: the font carries the flag and drawPage() paints
    // an explicit line so that the decoration also reaches the written content
    // stream. The tooltip states the granularity because the whole text box is
    // affected, not a character range.
    m_strikeOutButton = new QToolButton(decorationRow);
    m_strikeOutButton->setText(tr("S"));
    m_strikeOutButton->setCheckable(true);
    m_strikeOutButton->setToolTip(tr("删除线：作用于整个文本框，保存后以线段写入文件"));
    decorationLayout->addWidget(m_strikeOutButton);
    decorationLayout->addWidget(createDisabledPlaceholder(tr("T")));
    decorationLayout->addWidget(createDisabledPlaceholder(tr("T₁")));

    connect(m_boldButton, &QToolButton::toggled,
            this, &PDFContextualFormatPanel::onBoldToggled);
    connect(m_italicButton, &QToolButton::toggled,
            this, &PDFContextualFormatPanel::onItalicToggled);
    connect(m_underlineButton, &QToolButton::toggled,
            this, &PDFContextualFormatPanel::onUnderlineToggled);
    connect(m_strikeOutButton, &QToolButton::toggled,
            this, &PDFContextualFormatPanel::onStrikeOutToggled);
    styleLayout->addWidget(decorationRow);

    // Horizontal alignment (P0) + vertical alignment (P2, disabled)
    QWidget* alignmentRow = new QWidget(styleContent);
    QHBoxLayout* alignmentLayout = new QHBoxLayout(alignmentRow);
    alignmentLayout->setContentsMargins(0, 0, 0, 0);

    m_textAlignmentGroup = new QButtonGroup(this);
    m_textAlignmentGroup->setExclusive(true);

    const QStringList alignmentNames =
    {
        tr("左对齐"), tr("居中"), tr("右对齐"), tr("两端对齐")
    };
    for (int i = 0; i < alignmentNames.size(); ++i)
    {
        QToolButton* button = new QToolButton(alignmentRow);
        button->setObjectName(QStringLiteral("formatPanelTextAlignment"));
        button->setText(alignmentNames.at(i));
        button->setCheckable(true);
        alignmentLayout->addWidget(button);
        m_textAlignmentButtons.append(button);
        m_textAlignmentGroup->addButton(button, i);
        connect(button, &QToolButton::clicked, this,
                [this, i]() { onTextAlignmentClicked(i); });
    }
    alignmentLayout->addWidget(createDisabledPlaceholder(tr("顶端对齐")));
    alignmentLayout->addWidget(createDisabledPlaceholder(tr("垂直居中")));
    alignmentLayout->addWidget(createDisabledPlaceholder(tr("底端对齐")));
    styleLayout->addWidget(alignmentRow);

    // Inline, modeless text editor (R-06 downgrade A / R-05 FreeText input)
    QLabel* inlineHint = new QLabel(tr("文本内容（即时生效）"), styleContent);
    styleLayout->addWidget(inlineHint);

    m_inlineTextEdit = new QTextEdit(styleContent);
    m_inlineTextEdit->setObjectName(QStringLiteral("formatPanelInlineText"));
    m_inlineTextEdit->setMinimumHeight(QFontMetrics(font()).height() * 4);
    m_inlineTextEdit->setAcceptRichText(false);
    // The focus boundaries of the editor delimit one undo transaction, see
    // eventFilter() and beginInlineEditTransaction().
    m_inlineTextEdit->installEventFilter(this);
    styleLayout->addWidget(m_inlineTextEdit);

    connect(m_inlineTextEdit, &QTextEdit::textChanged,
            this, &PDFContextualFormatPanel::onInlineTextEdited);

    cardLayout->addWidget(createCollapsibleGroup(tr("Text Style"),
                                                QStringLiteral("textStyle"),
                                                styleContent, true));

    // --- Paragraph: disabled placeholders ----------------------------------
    QWidget* paragraphContent = new QWidget(card);
    QGridLayout* paragraphLayout = new QGridLayout(paragraphContent);
    paragraphLayout->setContentsMargins(0, 0, 0, 0);

    paragraphLayout->addWidget(createDisabledPlaceholder(tr("列表")), 0, 0);
    paragraphLayout->addWidget(createDisabledPlaceholder(tr("缩进")), 0, 1);

    QDoubleSpinBox* lineSpacing = new QDoubleSpinBox(paragraphContent);
    lineSpacing->setRange(0.0, 10.0);
    lineSpacing->setValue(1.0);
    lineSpacing->setEnabled(false);
    lineSpacing->setToolTip(tr("当前版本不支持"));
    paragraphLayout->addWidget(new QLabel(tr("行距"), paragraphContent), 1, 0);
    paragraphLayout->addWidget(lineSpacing, 1, 1);

    QDoubleSpinBox* indent = new QDoubleSpinBox(paragraphContent);
    indent->setRange(0.0, 100.0);
    indent->setValue(1.0);
    indent->setEnabled(false);
    indent->setToolTip(tr("当前版本不支持"));
    paragraphLayout->addWidget(new QLabel(tr("缩进"), paragraphContent), 2, 0);
    paragraphLayout->addWidget(indent, 2, 1);

    QDoubleSpinBox* paragraphSpacing = new QDoubleSpinBox(paragraphContent);
    paragraphSpacing->setRange(0.0, 100.0);
    paragraphSpacing->setValue(1.0);
    paragraphSpacing->setEnabled(false);
    paragraphSpacing->setToolTip(tr("当前版本不支持"));
    paragraphLayout->addWidget(new QLabel(tr("段间距"), paragraphContent), 3, 0);
    paragraphLayout->addWidget(paragraphSpacing, 3, 1);

    cardLayout->addWidget(createCollapsibleGroup(tr("段落"),
                                                QStringLiteral("paragraph"),
                                                paragraphContent, true));

    // --- Advanced: disabled placeholders -----------------------------------
    QWidget* advancedContent = new QWidget(card);
    QGridLayout* advancedLayout = new QGridLayout(advancedContent);
    advancedLayout->setContentsMargins(0, 0, 0, 0);

    QSpinBox* zoom = new QSpinBox(advancedContent);
    zoom->setRange(0, 1000);
    zoom->setValue(100);
    zoom->setSuffix(tr("%"));
    zoom->setEnabled(false);
    zoom->setToolTip(tr("当前版本不支持"));

    QDoubleSpinBox* letterSpacing = new QDoubleSpinBox(advancedContent);
    letterSpacing->setRange(0.0, 100.0);
    letterSpacing->setValue(1.0);
    letterSpacing->setEnabled(false);
    letterSpacing->setToolTip(tr("当前版本不支持"));

    QDoubleSpinBox* tracking = new QDoubleSpinBox(advancedContent);
    tracking->setRange(0.0, 100.0);
    tracking->setValue(1.0);
    tracking->setEnabled(false);
    tracking->setToolTip(tr("当前版本不支持"));

    advancedLayout->addWidget(new QLabel(tr("缩放"), advancedContent), 0, 0);
    advancedLayout->addWidget(zoom, 0, 1);
    advancedLayout->addWidget(new QLabel(tr("A|B 字距"), advancedContent), 1, 0);
    advancedLayout->addWidget(letterSpacing, 1, 1);
    advancedLayout->addWidget(new QLabel(tr("字距"), advancedContent), 2, 0);
    advancedLayout->addWidget(tracking, 2, 1);
    advancedLayout->addWidget(createDisabledPlaceholder(tr("多色环")), 3, 0);
    advancedLayout->addWidget(createDisabledPlaceholder(tr("吸管取色")), 3, 1);
    advancedLayout->addWidget(new QLabel(tr("书写方向"), advancedContent), 4, 0);
    advancedLayout->addWidget(createDisabledPlaceholder(tr("从左到右")), 4, 1);

    cardLayout->addWidget(createCollapsibleGroup(tr("高级"),
                                                QStringLiteral("advanced"),
                                                advancedContent, true));

    // --- Object arrange (R-16, element side) -------------------------------
    // The element side of R-16 (Page Center / Rotate / Alignment) needs an entry
    // point in the card, which is shown when a page content element is selected,
    // otherwise the element branch of the handlers would be unreachable. Flip and
    // Distribute stay disabled for elements (KI-04 / G-9, no equal gap operation
    // exists for page content elements).
    cardLayout->addWidget(createCollapsibleGroup(tr("Arrange"),
                                                QStringLiteral("textArrange"),
                                                createArrangeGroup(card, m_textArrange, true),
                                                true));
    cardLayout->addWidget(createCardFooterRow(&m_textSetAsDefaultButton));
    cardLayout->addStretch(1);
    return card;
}

QWidget* PDFContextualFormatPanel::createEmptyCard()
{
    QWidget* card = new QWidget(this);
    card->setObjectName(QStringLiteral("formatPanelEmptyCard"));
    QVBoxLayout* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(4);

    QLabel* hint = new QLabel(tr("选中页面上的标注或文本以编辑属性"), card);
    hint->setWordWrap(true);
    cardLayout->addWidget(hint);

    QLabel* distinction = new QLabel(tr("插入菜单中的“行内文本”生成 FreeText 注释，选中后显示标注卡；编辑器中的“创建文本标签”生成页面内容元素，选中后显示文字卡。"), card);
    distinction->setWordWrap(true);
    cardLayout->addWidget(distinction);

    QWidget* defaultsContent = new QWidget(card);
    QVBoxLayout* defaultsLayout = new QVBoxLayout(defaultsContent);
    defaultsLayout->setContentsMargins(0, 0, 0, 0);
    QLabel* defaultsHint = new QLabel(tr("默认样式 / Defaults：下一个待创建元素的样式仍由各创建工具保存，本面板不会覆盖工具默认值。"), defaultsContent);
    defaultsHint->setWordWrap(true);
    defaultsLayout->addWidget(defaultsHint);

    cardLayout->addWidget(createCollapsibleGroup(tr("默认样式 / Defaults"),
                                                QStringLiteral("defaults"),
                                                defaultsContent, false));
    cardLayout->addStretch(1);
    return card;
}

void PDFContextualFormatPanel::registerPageContentScene(PDFPageContentScene* scene)
{
    if (!scene)
    {
        return;
    }

    if (m_scene && m_scene != scene)
    {
        unregisterPageContentScene(m_scene);
    }

    m_scene = scene;
    connect(m_scene, &PDFPageContentScene::selectionChanged,
            this, &PDFContextualFormatPanel::onSceneSelectionChanged,
            Qt::UniqueConnection);
    connect(m_scene, &PDFPageContentScene::sceneChanged,
            this, &PDFContextualFormatPanel::onSceneChanged,
            Qt::UniqueConnection);
    m_lastSelectionSignature.clear();
    scheduleStateUpdate();
}

void PDFContextualFormatPanel::unregisterPageContentScene(PDFPageContentScene* scene)
{
    if (!m_scene || (scene && scene != m_scene))
    {
        return;
    }

    disconnect(m_scene, &PDFPageContentScene::selectionChanged,
               this, &PDFContextualFormatPanel::onSceneSelectionChanged);
    disconnect(m_scene, &PDFPageContentScene::sceneChanged,
               this, &PDFContextualFormatPanel::onSceneChanged);
    m_scene = nullptr;
    m_freeTextTool = nullptr;
    m_lastSelectionSignature.clear();
    scheduleStateUpdate();
}

void PDFContextualFormatPanel::showEvent(QShowEvent* event)
{
    QDockWidget::showEvent(event);
    if (m_toolPollTimer)
    {
        m_toolPollTimer->start();
    }
    scheduleStateUpdate();
}

void PDFContextualFormatPanel::hideEvent(QHideEvent* event)
{
    // A hidden panel never receives the FocusOut event of its inline editor.
    finishInlineEditTransaction();
    commitFreeTextContents();

    if (m_toolPollTimer)
    {
        m_toolPollTimer->stop();
    }
    QDockWidget::hideEvent(event);
}

void PDFContextualFormatPanel::onAnnotationSelectionChanged()
{
    scheduleStateUpdate();
}

void PDFContextualFormatPanel::onSceneSelectionChanged()
{
    scheduleStateUpdate();
}

void PDFContextualFormatPanel::onSceneChanged(bool graphicsOnly)
{
    if (graphicsOnly)
    {
        // Pure manipulation (drag / resize) changes no value shown by the
        // panel, so the cached signature of the selection stays valid.
        return;
    }

    // Elements were added, replaced or restored (write back, undo / redo),
    // so the next state update has to re-read the values of the selection.
    m_lastSelectionSignature.clear();
    scheduleStateUpdate();
}

void PDFContextualFormatPanel::onActiveToolChanged()
{
    scheduleStateUpdate();
}

void PDFContextualFormatPanel::onToolPollTimeout()
{
    PDFToolManager* toolManager = m_widget ? m_widget->getToolManager() : nullptr;
    PDFWidgetTool* tool = toolManager ? toolManager->getActiveTool() : nullptr;

    if (tool != m_lastActiveTool.data())
    {
        // The tool object was replaced by another one - the activity signal of
        // the previous tool must not stay connected.
        if (PDFWidgetTool* previousTool = m_lastActiveTool.data())
        {
            QObject::disconnect(previousTool, nullptr, this, nullptr);
        }

        m_lastActiveTool = tool;
        if (tool)
        {
            connect(tool, &PDFWidgetTool::toolActivityChanged,
                    this, &PDFContextualFormatPanel::onActiveToolChanged,
                    Qt::UniqueConnection);
        }

        refreshFreeTextToolCache();
        m_lastFreeTextEditingState = isFreeTextVisualEditing();
        m_lastSelectionSignature.clear();
        scheduleStateUpdate();
        return;
    }

    // The FreeText tool enters its visual editing state after a rectangle has
    // been picked and PDFToolManager has no activeToolChanged signal, so this
    // low frequency poll is the only reliable way to notice the transition.
    // Nothing else is polled - an unconditional state update here would
    // rewrite the inline text editor on every tick.
    refreshFreeTextToolCache();
    const bool freeTextEditing = isFreeTextVisualEditing();
    if (freeTextEditing != m_lastFreeTextEditingState)
    {
        m_lastFreeTextEditingState = freeTextEditing;
        m_lastSelectionSignature.clear();
        scheduleStateUpdate();
    }
}

void PDFContextualFormatPanel::refreshFreeTextToolCache()
{
    PDFToolManager* toolManager = m_widget ? m_widget->getToolManager() : nullptr;
    PDFWidgetTool* tool = toolManager ? toolManager->getActiveTool() : nullptr;
    m_freeTextTool = qobject_cast<PDFCreateFreeTextTool*>(tool);
}

QString PDFContextualFormatPanel::currentAnnotationAnchorSignature() const
{
    if (!m_annotationManager)
    {
        return QString();
    }

    std::vector<PDFObjectReference> references = m_annotationManager->getSelectedAnnotations();
    std::sort(references.begin(), references.end());

    QStringList parts;
    parts.reserve(static_cast<qsizetype>(references.size()));
    for (const PDFObjectReference& reference : references)
    {
        parts.append(QStringLiteral("%1:%2")
                         .arg(reference.objectNumber)
                         .arg(reference.generation));
    }

    return parts.join(QLatin1Char(','));
}

bool PDFContextualFormatPanel::isFreeTextVisualEditing() const
{
    return m_freeTextTool && m_freeTextTool->isVisualEditing();
}

bool PDFContextualFormatPanel::isTextToolActive()
{
    refreshFreeTextToolCache();
    if (m_freeTextTool)
    {
        return true;
    }

    PDFToolManager* toolManager = m_widget ? m_widget->getToolManager() : nullptr;
    PDFWidgetTool* tool = toolManager ? toolManager->getActiveTool() : nullptr;
    return qobject_cast<PDFCreatePCElementTextTool*>(tool) != nullptr;
}

bool PDFContextualFormatPanel::hasTextBoxSelection() const
{
    if (!m_scene)
    {
        return false;
    }

    for (PDFInteger id : m_scene->getSelectedElementIds())
    {
        // Images, rectangles, lines and curves must not open the text card.
        if (dynamic_cast<const PDFPageContentElementTextBox*>(m_scene->getElementById(id)))
        {
            return true;
        }
    }

    return false;
}

QString PDFContextualFormatPanel::buildSelectionSignature() const
{
    QStringList parts;

    if (m_annotationManager)
    {
        for (const PDFObjectReference& reference : m_annotationManager->getSelectedAnnotations())
        {
            parts.append(QStringLiteral("a%1.%2").arg(reference.objectNumber)
                                                  .arg(reference.generation));
        }
    }

    if (m_scene)
    {
        for (PDFInteger id : m_scene->getSelectedElementIds())
        {
            parts.append(QStringLiteral("e%1").arg(id));
        }
    }

    parts.append(isFreeTextVisualEditing() ? QStringLiteral("freetext")
                                           : QStringLiteral("nofreetext"));
    return parts.join(QLatin1Char('|'));
}

bool PDFContextualFormatPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_freeTextEdit && event->type() == QEvent::FocusOut)
    {
        // The user left the annotation text editor - commit immediately instead
        // of waiting for the debounce timer.
        commitFreeTextContents();
    }

    if (watched == m_inlineTextEdit)
    {
        switch (event->type())
        {
            case QEvent::FocusIn:
                beginInlineEditTransaction();
                break;

            case QEvent::FocusOut:
                finishInlineEditTransaction();
                break;

            default:
                break;
        }
    }

    return QDockWidget::eventFilter(watched, event);
}

void PDFContextualFormatPanel::beginInlineEditTransaction()
{
    if (m_inlineEditInTransaction)
    {
        return;
    }

    // The transaction only makes sense when typing really modifies an element of
    // the page content scene. The FreeText preview is not a part of the scene and
    // an empty selection has no element to write into, so opening a transaction
    // there would only push a redundant snapshot onto the undo stack.
    if (isFreeTextVisualEditing() || !hasTextBoxSelection())
    {
        return;
    }

    // One undo step for the whole typing session: EditorPlugin suppresses
    // createUndoStep() until this transaction is closed.
    m_inlineEditInTransaction = true;
    Q_EMIT continuousEditStarted();
}

void PDFContextualFormatPanel::finishInlineEditTransaction()
{
    if (!m_inlineEditInTransaction)
    {
        return;
    }

    m_inlineEditInTransaction = false;
    Q_EMIT continuousEditFinished();
}

void PDFContextualFormatPanel::scheduleStateUpdate()
{
    if (m_stateUpdatePending)
    {
        return;
    }

    m_stateUpdatePending = true;
    QTimer::singleShot(0, this, &PDFContextualFormatPanel::applyState);
}

void PDFContextualFormatPanel::applyState()
{
    if (m_isUpdatingFromModel)
    {
        // The guard flag is held at this moment, so the request must not be
        // dropped. The latch has to be released before re-scheduling, otherwise
        // scheduleStateUpdate() would coalesce the new request into nothing and
        // the panel would never be updated again.
        m_stateUpdatePending = false;
        scheduleStateUpdate();
        return;
    }

    m_stateUpdatePending = false;

    const bool annotationSelected = m_annotationManager && m_annotationManager->hasSelection();
    const bool textBoxSelected = hasTextBoxSelection();
    // isTextToolActive() also refreshes the cached FreeText tool pointer.
    const bool textToolIntent = isTextToolActive();

    // Fixed priority: an annotation selection always opens the annotation card,
    // so selecting a highlighted annotation can never show the text card. The
    // result does not depend on which selection happened last.
    Card newCard = Card::Empty;
    if (annotationSelected)
    {
        newCard = Card::Annotation;
    }
    else if (textBoxSelected)
    {
        newCard = Card::Text;
    }
    else if (textToolIntent)
    {
        newCard = Card::Text;
    }

    const bool cardChanged = (newCard != m_card);
    setCard(newCard);

    // The controls are re-read only when the card or the selection changed. An
    // update without a change must not touch them at all - it would reset the
    // cursor and the undo history of the inline text editor.
    const QString signature = buildSelectionSignature();
    if (cardChanged || signature != m_lastSelectionSignature)
    {
        refreshFromSelection();
    }
}

void PDFContextualFormatPanel::setCard(Card card)
{
    if (m_card == card)
    {
        return;
    }

    // Switching cards must never leave values of the previous object behind.
    clearCardValues();
    m_card = card;

    switch (card)
    {
        case Card::Annotation:
            m_stack->setCurrentWidget(m_annotationCard);
            break;
        case Card::Text:
            m_stack->setCurrentWidget(m_textCard);
            break;
        case Card::Empty:
            m_stack->setCurrentWidget(m_emptyCard);
            break;
    }
}

void PDFContextualFormatPanel::clearCardValues()
{
    // The values of the previous object are dropped below and the inline
    // editor is cleared, so an open editing session is closed first. This is
    // the fallback for a card switch without a FocusOut event of the editor.
    finishInlineEditTransaction();

    // The pending text of the free text editor belongs to the object which is
    // being left, so it is written back before the editor is cleared.
    commitFreeTextContents();

    // Guard flag + signal blockers: switching cards must never write back.
    m_isUpdatingFromModel = true;

    ControlSignalBlockers blockers({ this,
                                     m_opacitySlider,
                                     m_opacitySpinBox,
                                     m_fontCombo,
                                     m_fontSizeSpinBox,
                                     m_boldButton,
                                     m_italicButton,
                                     m_underlineButton,
                                     m_strikeOutButton,
                                     m_lockedCheckBox,
                                     m_textColorButton,
                                     m_freeTextEdit,
                                     m_inlineTextEdit,
                                     m_annotationArrange.rotateAngle,
                                     m_textArrange.rotateAngle });

    for (QToolButton* button : m_colorButtons)
    {
        button->setChecked(false);
    }

    m_opacitySlider->setValue(0);
    m_opacitySpinBox->setSpecialValueText(QString());
    m_opacitySpinBox->setValue(0);
    m_mixedHintLabel->clear();

    m_freeTextEdit->clear();
    m_freeTextEdit->setEnabled(false);

    m_fontCombo->setCurrentFont(QFont(QStringLiteral("Helvetica"), 10));
    m_fontSizeSpinBox->setValue(10.0);
    m_boldButton->setChecked(false);
    m_italicButton->setChecked(false);
    m_underlineButton->setChecked(false);
    m_strikeOutButton->setChecked(false);
    if (m_lockedCheckBox)
    {
        m_lockedCheckBox->setCheckState(Qt::Unchecked);
    }
    m_inlineTextEdit->clear();
    m_annotationArrange.rotateAngle->setValue(0.0);
    m_textArrange.rotateAngle->setValue(0.0);

    for (QToolButton* button : m_textAlignmentButtons)
    {
        button->setChecked(false);
    }

    m_annotationColor = Qt::yellow;
    m_annotationOpacity = 100;
    m_annotationColorMixed = false;
    m_annotationOpacityMixed = false;
    m_textFont = QFont(QStringLiteral("Helvetica"), 10);
    m_textColor = Qt::black;
    m_textAlignment = Qt::AlignLeft;
    updatePreview();

    m_isUpdatingFromModel = false;
}

void PDFContextualFormatPanel::refreshFromSelection()
{
    if (m_isUpdatingFromModel)
    {
        return;
    }

    m_isUpdatingFromModel = true;

    QList<QObject*> controls;
    controls.append(this);
    controls.append(m_opacitySlider);
    controls.append(m_opacitySpinBox);
    controls.append(m_fontCombo);
    controls.append(m_fontSizeSpinBox);
    controls.append(m_boldButton);
    controls.append(m_italicButton);
    controls.append(m_underlineButton);
    controls.append(m_textColorButton);
    controls.append(m_freeTextEdit);
    controls.append(m_inlineTextEdit);
    controls.append(m_textAlignmentGroup);
    // The relative rotation angle is reset by every model -> panel update, so its
    // signal must never reach the document (the value is an increment only).
    controls.append(m_annotationArrange.rotateAngle);
    controls.append(m_textArrange.rotateAngle);
    for (QToolButton* button : m_colorButtons)
    {
        controls.append(button);
    }
    for (QToolButton* button : m_textAlignmentButtons)
    {
        controls.append(button);
    }
    ControlSignalBlockers blockers(controls);

    switch (m_card)
    {
        case Card::Annotation:
            readAnnotationValues();
            break;
        case Card::Text:
            readTextElementValues();
            break;
        case Card::Empty:
            // Empty state: every P0 control stays disabled, no stale values.
            for (QToolButton* button : m_colorButtons)
            {
                button->setEnabled(false);
            }
            m_opacitySlider->setEnabled(false);
            m_opacitySpinBox->setEnabled(false);
            m_fontCombo->setEnabled(false);
            m_fontSizeSpinBox->setEnabled(false);
            m_textColorButton->setEnabled(false);
            m_boldButton->setEnabled(false);
            m_italicButton->setEnabled(false);
            m_underlineButton->setEnabled(false);
            m_inlineTextEdit->setEnabled(false);
            m_freeTextEdit->setEnabled(false);
            for (QToolButton* button : m_textAlignmentButtons)
            {
                button->setEnabled(false);
            }
            updateArrangeAvailability();
            break;
    }

    m_isUpdatingFromModel = false;
    m_lastSelectionSignature = buildSelectionSignature();
}

void PDFContextualFormatPanel::readAnnotationValues()
{
    if (!m_annotationManager || !m_annotationManager->hasSelection())
    {
        return;
    }

    const std::optional<QColor> color = m_annotationManager->getSelectedAnnotationsColor();
    const std::optional<PDFReal> opacity = m_annotationManager->getSelectedAnnotationsOpacity();

    m_annotationColorMixed = !color.has_value();
    m_annotationOpacityMixed = !opacity.has_value();
    m_annotationColor = color.value_or(QColor(Qt::yellow));
    m_annotationOpacity = opacity.has_value()
                              ? qBound(0, static_cast<int>(qRound(opacity.value() * 100.0)), 100)
                              : 100;

    for (QToolButton* button : m_colorButtons)
    {
        // The two P2 placeholders (multicolor ring / eyedropper) stay disabled.
        const int index = m_colorButtons.indexOf(button);
        const QColor paletteColor = (index >= 0 && index < m_colorPalette.size())
                                        ? m_colorPalette.at(index) : QColor();
        button->setEnabled(paletteColor.isValid());
    }

    if (m_annotationColorMixed)
    {
        for (QToolButton* button : m_colorButtons)
        {
            button->setChecked(false);
        }
        m_mixedHintLabel->setText(tr("混合 (Mixed)"));
    }
    else
    {
        m_mixedHintLabel->clear();
        for (int i = 0; i < m_colorButtons.size(); ++i)
        {
            const QColor paletteColor = m_colorPalette.at(i);
            m_colorButtons.at(i)->setChecked(paletteColor.isValid() &&
                                             paletteColor.toRgb() == m_annotationColor.toRgb());
        }
    }

    m_opacitySlider->setEnabled(true);
    m_opacitySpinBox->setEnabled(true);

    if (m_annotationOpacityMixed)
    {
        m_opacitySlider->setValue(0);
        m_opacitySpinBox->setSpecialValueText(tr("混合 (Mixed)"));
        m_opacitySpinBox->setValue(0);
    }
    else
    {
        m_opacitySpinBox->setSpecialValueText(QString());
        m_opacitySlider->setValue(m_annotationOpacity);
        m_opacitySpinBox->setValue(m_annotationOpacity);
    }

    // Free text annotations only - the "Contents" entry is the text painted
    // into the appearance stream of the annotation.
    const bool freeTextSelection = m_annotationManager->hasSelectedFreeTextAnnotations();
    const std::optional<QString> freeTextContents = m_annotationManager->getSelectedFreeTextContents();

    m_freeTextEdit->setEnabled(freeTextSelection);
    m_freeTextEdit->setToolTip(freeTextSelection ? tr("注释文字（即时生效）")
                                                 : tr("仅自由文本注释支持编辑文字"));

    // The editor is refilled, unless the user is really editing this very
    // selection: the editor has the focus AND the selection did not change
    // since the last backfill. Otherwise a pending write back (see
    // onFreeTextEdited()) would be lost, or - worse - the text of the previous
    // annotation would stay in the editor after a selection change.
    const QString anchorSignature = currentAnnotationAnchorSignature();
    if (!(m_freeTextEdit->hasFocus() && anchorSignature == m_freeTextAnchorSignature))
    {
        if (freeTextContents.has_value())
        {
            if (m_freeTextEdit->toPlainText() != freeTextContents.value())
            {
                const QTextCursor oldCursor = m_freeTextEdit->textCursor();
                const int oldAnchor = oldCursor.anchor();
                const int oldPosition = oldCursor.position();

                m_freeTextEdit->setPlainText(freeTextContents.value());

                const int length = m_freeTextEdit->toPlainText().size();
                QTextCursor cursor = m_freeTextEdit->textCursor();
                cursor.setPosition(qBound(0, qMin(oldAnchor, oldPosition), length));
                cursor.setPosition(qBound(0, qMax(oldAnchor, oldPosition), length),
                                   QTextCursor::KeepAnchor);
                m_freeTextEdit->setTextCursor(cursor);
            }
        }
        else
        {
            // No selection, a non free text selection, or mixed texts - the
            // placeholder "混合 (Mixed)" is shown for the mixed case. The empty
            // editor is not dirty, so leaving it can never clear the selection.
            m_freeTextEdit->clear();
        }

        // The editor now shows this selection, and its text is not modified.
        m_freeTextAnchorSignature = anchorSignature;
        m_freeTextDirty = false;
    }

    // R-22: mirror the /F Locked flag (tristate for mixed selections) and grey
    // out the direct write back controls while locked - the existing
    // canModifyAnnotation() / canTransformAnnotation() filters would reject the
    // writes anyway, so the panel must not present them as usable.
    std::optional<bool> selectedLocked;
    if (m_lockedCheckBox)
    {
        selectedLocked = m_annotationManager->getSelectedAnnotationsLocked();
        m_lockedCheckBox->setCheckState(selectedLocked.has_value()
                                            ? (selectedLocked.value() ? Qt::Checked : Qt::Unchecked)
                                            : Qt::PartiallyChecked);
    }

    if (selectedLocked.value_or(false))
    {
        for (QToolButton* button : m_colorButtons)
        {
            button->setEnabled(false);
        }
        m_opacitySlider->setEnabled(false);
        m_opacitySpinBox->setEnabled(false);
        m_mixedHintLabel->setText(tr("对象已锁定，先取消锁定再修改"));
    }

    updateArrangeAvailability();
    updatePreview();
}

void PDFContextualFormatPanel::readTextElementValues()
{
    // The active tool can be replaced at any time, so the cached pointer must be
    // refreshed before the FreeText preview state is read.
    refreshFreeTextToolCache();

    QFont font = QFont(QStringLiteral("Helvetica"), 10);
    QColor color = Qt::black;
    Qt::Alignment alignment = Qt::AlignLeft;
    QString text;

    bool textBoxFound = false;
    if (m_scene)
    {
        const std::set<PDFInteger> selectedIds = m_scene->getSelectedElementIds();
        for (PDFInteger id : selectedIds)
        {
            const PDFPageContentElement* element = m_scene->getElementById(id);
            const PDFPageContentElementTextBox* textBox =
                dynamic_cast<const PDFPageContentElementTextBox*>(element);
            if (textBox)
            {
                font = textBox->getFont();
                color = textBox->getPen().color();
                alignment = textBox->getAlignment();
                text = textBox->getText();
                textBoxFound = true;
                break;
            }
        }
    }

    const bool freeTextEditing = isFreeTextVisualEditing();
    if (freeTextEditing)
    {
        const PDFFreeTextStyle style = m_freeTextTool->getVisualStyle();
        font = QFont(style.fontFamily);
        if (style.fontSize > 0.0)
        {
            font.setPointSizeF(style.fontSize);
        }
        color = style.textColor;
        alignment = Qt::Alignment(style.textAlignment) & Qt::AlignHorizontal_Mask;
        if (alignment == 0)
        {
            alignment = Qt::AlignLeft;
        }
        text = m_freeTextTool->getVisualText();
    }

    const bool textToolActive = isTextToolActive();

    // Font family, font size, color and alignment are written either into the
    // style of the FreeText annotation being created, into the selected text box,
    // or into the active text creation tool.
    const bool styleEditable = freeTextEditing || textBoxFound || textToolActive;
    // PDFFreeTextStyle has only four fields (font family, font size, text color
    // and alignment), so bold / italic / underline cannot be stored in it. The
    // three buttons are disabled during the FreeText preview instead of being
    // re-checked from an incomplete font on every state update.
    const bool decorationEditable = (textBoxFound || textToolActive) && !freeTextEditing;
    // The inline editor can change the text of a text box and the text of the
    // FreeText annotation being created.
    const bool contentEditable = freeTextEditing || textBoxFound;

    const QString unsupportedTip = tr("当前版本不支持");
    const QString selectTargetTip = tr("请先选中一个文本框");

    m_fontCombo->setEnabled(styleEditable);
    m_fontCombo->setToolTip(styleEditable ? QString() : selectTargetTip);
    m_fontSizeSpinBox->setEnabled(styleEditable);
    m_fontSizeSpinBox->setToolTip(styleEditable ? QString() : selectTargetTip);
    m_textColorButton->setEnabled(styleEditable);
    m_textColorButton->setToolTip(styleEditable ? tr("文字颜色") : selectTargetTip);

    m_boldButton->setEnabled(decorationEditable);
    m_italicButton->setEnabled(decorationEditable);
    m_underlineButton->setEnabled(decorationEditable);
    const QString decorationTip = decorationEditable ? QString() : unsupportedTip;
    m_boldButton->setToolTip(decorationTip);
    m_italicButton->setToolTip(decorationTip);
    m_underlineButton->setToolTip(decorationTip);

    m_inlineTextEdit->setEnabled(contentEditable);
    for (QToolButton* button : m_textAlignmentButtons)
    {
        button->setEnabled(styleEditable);
        button->setToolTip(styleEditable ? QString() : selectTargetTip);
    }

    updateTextControlsFromValues(font, color, alignment, !freeTextEditing);

    // setPlainText() resets the cursor, the selection and the undo history of
    // the inline editor, so it is called only when the text really differs. The
    // cursor position and the anchor are restored (clamped to the new length).
    if (m_inlineTextEdit->toPlainText() != text)
    {
        const QTextCursor oldCursor = m_inlineTextEdit->textCursor();
        const int oldAnchor = oldCursor.anchor();
        const int oldPosition = oldCursor.position();

        m_inlineTextEdit->setPlainText(text);

        const int length = m_inlineTextEdit->toPlainText().size();
        QTextCursor cursor = m_inlineTextEdit->textCursor();
        cursor.setPosition(qBound(0, qMin(oldAnchor, oldPosition), length));
        cursor.setPosition(qBound(0, qMax(oldAnchor, oldPosition), length),
                           QTextCursor::KeepAnchor);
        m_inlineTextEdit->setTextCursor(cursor);
    }

    updateArrangeAvailability();
}

void PDFContextualFormatPanel::updateTextControlsFromValues(const QFont& font,
                                                           const QColor& color,
                                                           Qt::Alignment alignment,
                                                           bool applyDecorationState)
{
    m_textFont = font;
    m_textColor = color.isValid() ? color : QColor(Qt::black);
    m_textAlignment = alignment;

    m_fontCombo->setCurrentFont(font);
    const double size = font.pointSizeF() > 0.0 ? font.pointSizeF() : 10.0;
    m_fontSizeSpinBox->setValue(size);

    if (applyDecorationState)
    {
        // The decoration state is not a part of PDFFreeTextStyle, so it must
        // not be derived from the font of the FreeText preview.
        m_boldButton->setChecked(font.bold());
        m_italicButton->setChecked(font.italic());
        m_underlineButton->setChecked(font.underline());
        m_strikeOutButton->setChecked(font.strikeOut());
    }

    m_textColorButton->setIcon(createColorIcon(m_textColor,
                                               QFontMetrics(font).height() + 6));

    int alignmentIndex = 0;
    if (alignment.testFlag(Qt::AlignHCenter))
    {
        alignmentIndex = 1;
    }
    else if (alignment.testFlag(Qt::AlignRight))
    {
        alignmentIndex = 2;
    }
    else if (alignment.testFlag(Qt::AlignJustify))
    {
        alignmentIndex = 3;
    }

    for (int i = 0; i < m_textAlignmentButtons.size(); ++i)
    {
        m_textAlignmentButtons.at(i)->setChecked(i == alignmentIndex);
    }
}

void PDFContextualFormatPanel::applyTextPropertiesToTargets(const QFont& font,
                                                           const QColor& color,
                                                           Qt::Alignment alignment)
{
    m_textFont = font;
    m_textColor = color;
    m_textAlignment = alignment;

    // The active tool can be replaced at any time, so the cached pointer is
    // refreshed before it is used.
    refreshFreeTextToolCache();
    if (isFreeTextVisualEditing())
    {
        // Creation of a FreeText annotation: write into the existing four
        // PDFFreeTextStyle fields and refresh the on-page preview immediately.
        m_freeTextTool->updateStyleFromPanel(font, alignment, color);
    }
}

void PDFContextualFormatPanel::openAdvancedDialog()
{
    if (!m_advancedDialog)
    {
        m_advancedDialog = new PDFAdvancedFormatDialog(this);
        connect(m_advancedDialog, &PDFAdvancedFormatDialog::borderWidthChanged,
                this, &PDFContextualFormatPanel::onAdvancedBorderWidthChanged);
        connect(m_advancedDialog, &PDFAdvancedFormatDialog::scalePercentRequested,
                this, &PDFContextualFormatPanel::onAdvancedScalePercentRequested);
    }

    const int annotationCount = m_annotationManager
                                    ? static_cast<int>(m_annotationManager->getSelectedAnnotations().size())
                                    : 0;
    int elementCount = 0;
    const bool textSelected = hasTextOnlyElementSelection(&elementCount) && elementCount > 0;

    // The border width of the current selection cannot be read back: the manager
    // exposes only the object references of the selection, and PDFAnnotationBorder
    // is reachable through the page objects. The dialog therefore starts at the
    // PDF default and acts as a setter (registered as a known limitation).
    m_advancedDialog->setValues(annotationCount > 0,
                                1.0,
                                annotationCount > 0 ? tr("（当前版本不支持修改）") : QString(),
                                textSelected);
    m_advancedDialog->show();
    m_advancedDialog->raise();
    m_advancedDialog->activateWindow();
}

void PDFContextualFormatPanel::onAdvancedBorderWidthChanged(qreal width)
{
    if (m_isUpdatingFromModel || !m_annotationManager || !m_annotationManager->hasSelection())
    {
        return;
    }
    m_annotationManager->setSelectedAnnotationsBorderWidth(width);
}

void PDFContextualFormatPanel::onAdvancedScalePercentRequested(qreal percent)
{
    if (m_isUpdatingFromModel || percent <= 0.0)
    {
        return;
    }

    int elementCount = 0;
    if (!hasTextOnlyElementSelection(&elementCount) || elementCount <= 0)
    {
        return;
    }

    // The percentage is a one shot increment of the current font size, so it is
    // applied to the cached panel font and written back through the same signals
    // the panel already uses (no new write back path).
    QFont updated = m_textFont;
    const qreal current = updated.pointSizeF() > 0.0 ? updated.pointSizeF() : 10.0;
    updated.setPointSizeF(qBound(1.0, current * percent / 100.0, 500.0));
    applyTextPropertiesToTargets(updated, m_textColor, m_textAlignment);
    if (!isFreeTextVisualEditing())
    {
        Q_EMIT fontChanged(updated);
    }
}

void PDFContextualFormatPanel::updateSetAsDefaultAvailability()
{
    const QString availableTip = tr("把当前样式写入后续新建对象的默认样式（选中的对象本身不改变）");
    const QString noSelectionTip = tr("请先选中对象");

    const int annotationCount = m_annotationManager
                                    ? static_cast<int>(m_annotationManager->getSelectedAnnotations().size())
                                    : 0;
    const bool annotationsLocked = m_annotationManager
                                       ? m_annotationManager->getSelectedAnnotationsLocked().value_or(false)
                                       : false;
    if (m_annotationSetAsDefaultButton)
    {
        // A locked object must not feed the tool defaults, the checkbox is the
        // only control that stays usable.
        const bool enabled = annotationCount > 0 && !annotationsLocked;
        m_annotationSetAsDefaultButton->setEnabled(enabled);
        m_annotationSetAsDefaultButton->setToolTip(enabled ? availableTip
                                                           : (annotationsLocked ? tr("对象已锁定，先取消锁定再修改")
                                                                                : noSelectionTip));
    }

    int elementCount = 0;
    const bool textOnly = hasTextOnlyElementSelection(&elementCount);
    if (m_textSetAsDefaultButton)
    {
        const bool enabled = textOnly && elementCount > 0;
        m_textSetAsDefaultButton->setEnabled(enabled);
        m_textSetAsDefaultButton->setToolTip(enabled ? availableTip : noSelectionTip);
    }
}

void PDFContextualFormatPanel::onSetAsDefaultClicked()
{
    QToolButton* button = qobject_cast<QToolButton*>(sender());
    const bool annotationSide = (button && button == m_annotationSetAsDefaultButton);

    if (annotationSide)
    {
        if (!m_annotationManager || !m_annotationManager->hasSelection())
        {
            return;
        }
        // Only the colour is promoted: PDFAnnotationStyle has no opacity member
        // (KI-06 / G-14), so the slider of the card is intentionally left out.
        Q_EMIT annotationDefaultStyleRequested(m_annotationColor);
    }
    else
    {
        int elementCount = 0;
        if (!hasTextOnlyElementSelection(&elementCount) || elementCount <= 0)
        {
            return;
        }
        // The current panel values are passed explicitly, because the plugin has
        // no access to the panel state. The selected elements are not modified
        // here and must not be modified by the receiver (AC-14).
        Q_EMIT setAsDefaultRequested(m_textFont, m_textColor, m_textAlignment);
    }

    // Light weight confirmation: the button appears pressed for a moment and its
    // tooltip confirms the action. No dialog and no message box is created.
    if (button)
    {
        button->setDown(true);
        button->setToolTip(tr("已设为默认"));
        QTimer::singleShot(600, button, [button]()
        {
            button->setDown(false);
            button->setToolTip(tr("把当前样式写入后续新建对象的默认样式（选中的对象本身不改变）"));
        });
    }
}

void PDFContextualFormatPanel::onLockedToggled(bool checked)
{
    if (m_isUpdatingFromModel || !m_annotationManager || !m_annotationManager->hasSelection())
    {
        return;
    }

    // The partially checked state is display only (mixed selection); a user click
    // always lands on checked / unchecked.
    if (m_lockedCheckBox && m_lockedCheckBox->checkState() == Qt::PartiallyChecked)
    {
        return;
    }

    m_annotationManager->setSelectedAnnotationsLocked(checked);

    // Re-read everything: while locked, the colour / opacity controls and the
    // arrange group of the annotation card are greyed out, and the tristate
    // checkbox must reflect the flag which was just written.
    refreshFromSelection();
}

void PDFContextualFormatPanel::updateArrangeAvailability()
{
    const int annotationCount = m_annotationManager
                                    ? static_cast<int>(m_annotationManager->getSelectedAnnotations().size())
                                    : 0;
    const bool annotationsLocked = m_annotationManager
                                       ? m_annotationManager->getSelectedAnnotationsLocked().value_or(false)
                                       : false;
    updateArrangeGroup(m_annotationArrange, annotationCount, true, true);

    // R-22: while locked, the geometry group of the annotation card is unusable
    // (the existing transform filters would reject the writes anyway).
    if (annotationsLocked)
    {
        const QString lockedTip = tr("对象已锁定，先取消锁定再修改");
        ArrangeControls& controls = m_annotationArrange;
        controls.pageCenter->setEnabled(false);
        controls.pageCenter->setToolTip(lockedTip);
        controls.rotateClockwise->setEnabled(false);
        controls.rotateClockwise->setToolTip(lockedTip);
        controls.rotateCounterClockwise->setEnabled(false);
        controls.rotateCounterClockwise->setToolTip(lockedTip);
        controls.rotateAngle->setEnabled(false);
        controls.rotateAngle->setToolTip(lockedTip);
        controls.flipHorizontal->setEnabled(false);
        controls.flipVertical->setEnabled(false);
        for (QToolButton* alignButton : controls.alignButtons)
        {
            alignButton->setEnabled(false);
            alignButton->setToolTip(lockedTip);
        }
        for (QToolButton* distributeButton : controls.distributeButtons)
        {
            distributeButton->setEnabled(false);
            distributeButton->setToolTip(lockedTip);
        }
    }

    int elementCount = 0;
    const bool textOnly = hasTextOnlyElementSelection(&elementCount);
    updateArrangeGroup(m_textArrange, elementCount, textOnly, false);

    updateSetAsDefaultAvailability();
}

void PDFContextualFormatPanel::updateArrangeGroup(const ArrangeControls& controls,
                                                  int count,
                                                  bool rotateSupported,
                                                  bool flipSupported)
{
    const QString unsupportedTip = tr("当前版本不支持");
    const QString noSelectionTip = tr("请先选中对象");

    controls.pageCenter->setEnabled(count >= 1);

    const bool canRotate = count >= 1 && rotateSupported;
    controls.rotateClockwise->setEnabled(canRotate);
    controls.rotateCounterClockwise->setEnabled(canRotate);
    controls.rotateAngle->setEnabled(canRotate);
    const QString rotateTip = canRotate ? QString()
                                        : (count >= 1 ? unsupportedTip : noSelectionTip);
    controls.rotateClockwise->setToolTip(rotateTip.isEmpty() ? tr("顺时针旋转 90°") : rotateTip);
    controls.rotateCounterClockwise->setToolTip(rotateTip.isEmpty() ? tr("逆时针旋转 90°") : rotateTip);
    controls.rotateAngle->setToolTip(rotateTip.isEmpty()
                                         ? tr("旋转增量(°)：回车后按增量旋转一次，然后自动归零")
                                         : rotateTip);

    const bool canFlip = count >= 1 && flipSupported;
    controls.flipHorizontal->setEnabled(canFlip);
    controls.flipVertical->setEnabled(canFlip);
    const QString flipTip = canFlip ? QString() : unsupportedTip;
    controls.flipHorizontal->setToolTip(flipTip.isEmpty() ? tr("左右镜像") : flipTip);
    controls.flipVertical->setToolTip(flipTip.isEmpty() ? tr("上下镜像") : flipTip);

    for (QToolButton* button : controls.alignButtons)
    {
        button->setEnabled(count >= 2);
    }

    // Element side has no equal gap operation, so the buttons stay disabled and
    // must never call the annotation manager.
    for (QToolButton* button : controls.distributeButtons)
    {
        button->setEnabled(count >= 3 && !controls.elementSide);
    }
}

bool PDFContextualFormatPanel::hasTextOnlyElementSelection(int* count) const
{
    int elementCount = 0;
    bool textOnly = true;
    if (m_scene)
    {
        for (PDFInteger id : m_scene->getSelectedElementIds())
        {
            ++elementCount;
            const PDFPageContentElement* element = m_scene->getElementById(id);
            if (!element || !element->asElementTextBox())
            {
                textOnly = false;
            }
        }
    }

    if (count)
    {
        *count = elementCount;
    }

    return elementCount > 0 && textOnly;
}

pdf::PDFReal PDFContextualFormatPanel::selectedTextAngle() const
{
    if (!m_scene)
    {
        return 0.0;
    }

    for (PDFInteger id : m_scene->getSelectedElementIds())
    {
        const PDFPageContentElement* element = m_scene->getElementById(id);
        if (element)
        {
            if (const PDFPageContentElementTextBox* textBox = element->asElementTextBox())
            {
                return textBox->getAngle();
            }
        }
    }

    return 0.0;
}

void PDFContextualFormatPanel::rotateTargets(bool elementSide, pdf::PDFReal deltaDegrees)
{
    if (qFuzzyIsNull(deltaDegrees))
    {
        return;
    }

    if (elementSide)
    {
        int count = 0;
        if (!hasTextOnlyElementSelection(&count))
        {
            return;
        }

        // The plugin writes the absolute angle of the text box, so the delta is
        // added to the angle of the current selection here. A multiple selection
        // of text boxes with different angles is rotated to the same absolute
        // angle (the first selected text box is the reference).
        //
        // The delta is clockwise positive (the convention of this panel and of
        // PDFWidgetAnnotationManager), while the text box angle is counterclockwise
        // positive (drawPage() flips the y axis before painter->rotate()), so the
        // sign is inverted here. Static reasoning only - runtime to be confirmed.
        Q_EMIT textAngleChanged(selectedTextAngle() - deltaDegrees);
        return;
    }

    if (m_annotationManager && m_annotationManager->hasSelection())
    {
        m_annotationManager->rotateSelectedAnnotations(deltaDegrees);
    }
}

void PDFContextualFormatPanel::updatePreview()
{
    if (m_preview)
    {
        m_preview->setValues(m_annotationColor, m_annotationOpacity);
    }
}

void PDFContextualFormatPanel::onColorButtonClicked(int index)
{
    if (m_isUpdatingFromModel || !m_annotationManager ||
        !m_annotationManager->hasSelection() ||
        index < 0 || index >= m_colorPalette.size())
    {
        return;
    }

    const QColor color = m_colorPalette.at(index);
    if (!color.isValid())
    {
        return;
    }

    m_annotationColor = color;
    m_annotationColorMixed = false;
    m_mixedHintLabel->clear();
    m_annotationManager->setSelectedAnnotationsColor(color);

    for (int i = 0; i < m_colorButtons.size(); ++i)
    {
        m_colorButtons.at(i)->setChecked(i == index);
    }
    updatePreview();
}

void PDFContextualFormatPanel::onOpacityChanged(int value)
{
    if (m_isUpdatingFromModel || !m_annotationManager ||
        !m_annotationManager->hasSelection())
    {
        return;
    }

    m_annotationOpacity = qBound(0, value, 100);
    m_annotationOpacityMixed = false;

    {
        QSignalBlocker blocker(m_opacitySpinBox);
        m_opacitySpinBox->setSpecialValueText(QString());
        m_opacitySpinBox->setValue(m_annotationOpacity);
    }

    // Continuous: valueChanged drives the write back, never sliderReleased.
    m_annotationManager->setSelectedAnnotationsOpacity(
        static_cast<PDFReal>(m_annotationOpacity) / 100.0);
    updatePreview();
}

void PDFContextualFormatPanel::onOpacitySpinBoxChanged(int value)
{
    if (m_isUpdatingFromModel)
    {
        return;
    }

    QSignalBlocker blocker(m_opacitySlider);
    m_opacitySlider->setValue(value);
    onOpacityChanged(value);
}

void PDFContextualFormatPanel::onInlineColorSelected(const QColor& color)
{
    if (m_isUpdatingFromModel || !color.isValid())
    {
        return;
    }

    m_textColor = color;
    m_textColorButton->setIcon(createColorIcon(color, QFontMetrics(font()).height() + 6));

    refreshFreeTextToolCache();
    if (isFreeTextVisualEditing())
    {
        m_freeTextTool->updateStyleFromPanel(m_textFont, m_textAlignment, color);
    }
    else
    {
        Q_EMIT penChanged(QPen(color));
    }
}

void PDFContextualFormatPanel::onArrangePageCenter(bool elementSide)
{
    if (m_isUpdatingFromModel)
    {
        return;
    }

    if (elementSide)
    {
        if (m_scene && !m_scene->getSelectedElementIds().empty())
        {
            // PDFPageContentScene::performOperation() takes the integer value of
            // PDFPageContentElementManipulator::Operation and lets the manipulator
            // translate / replace the selected elements (one undo step).
            m_scene->performOperation(static_cast<int>(PDFPageContentElementManipulator::Operation::CenterHorAndVert));
        }
        return;
    }

    if (m_annotationManager && m_annotationManager->hasSelection())
    {
        m_annotationManager->centerSelectedAnnotationsOnPage();
    }
}

void PDFContextualFormatPanel::onArrangeRotate(bool elementSide, int direction)
{
    if (m_isUpdatingFromModel || (direction != 1 && direction != -1))
    {
        return;
    }

    if (elementSide)
    {
        // The text box is drawn with a flipped y axis (drawPage() applies
        // scale(1.0, -1.0) before painter->rotate()), so a positive text angle
        // turns the text counterclockwise on the screen, while the delta used by
        // the panel is clockwise positive (the same convention as the annotation
        // side). Verified statically only - a runtime check is required (see
        // 03-implementation-notes.md).
        rotateTargets(true, pdf::PDFReal(direction) * 90.0);
        return;
    }

    // The manager uses the screen orientation: a positive angle is clockwise.
    rotateTargets(false, pdf::PDFReal(direction) * 90.0);
}

void PDFContextualFormatPanel::onArrangeRotateAngle(bool elementSide, double deltaDegrees)
{
    if (m_isUpdatingFromModel)
    {
        return;
    }

    // The spin box shows a relative angle and is an increment only, so its value
    // is always reset to zero after the commit - otherwise the same angle would
    // be applied again on the next selection change or the next commit (AC-16:
    // entering 45 twice must rotate by 90 degrees in total, not by 135).
    QDoubleSpinBox* spinBox = elementSide ? m_textArrange.rotateAngle
                                          : m_annotationArrange.rotateAngle;
    const QSignalBlocker blocker(spinBox);
    spinBox->setValue(0.0);

    rotateTargets(elementSide, pdf::PDFReal(deltaDegrees));
}

void PDFContextualFormatPanel::onArrangeFlip(bool elementSide, Qt::Orientation orientation)
{
    if (m_isUpdatingFromModel)
    {
        return;
    }

    // Page content elements have no flip representation (the text box stores the
    // text, the rectangle and the angle only), so the element side stays
    // disabled and must never reach the annotation manager (KI-04 / G-9).
    if (elementSide || !m_annotationManager || !m_annotationManager->hasSelection())
    {
        return;
    }

    m_annotationManager->flipSelectedAnnotations(orientation);
}

void PDFContextualFormatPanel::onArrangeAlignment(int id, bool elementSide)
{
    if (m_isUpdatingFromModel || id < 0 || id > 5)
    {
        return;
    }

    if (elementSide)
    {
        if (!m_scene || m_scene->getSelectedElementIds().size() < 2)
        {
            return;
        }

        // G-15: the elements are aligned to the frame of the selection, there is
        // no anchor on the element side.
        using Operation = PDFPageContentElementManipulator::Operation;
        Operation operation = Operation::AlignLeft;
        switch (id)
        {
            case 0: operation = Operation::AlignLeft; break;
            case 1: operation = Operation::AlignCenterHorizontally; break;
            case 2: operation = Operation::AlignRight; break;
            case 3: operation = Operation::AlignTop; break;
            case 4: operation = Operation::AlignCenterVertically; break;
            case 5: operation = Operation::AlignBottom; break;
        }

        m_scene->performOperation(static_cast<int>(operation));
        return;
    }

    if (!m_annotationManager || m_annotationManager->getSelectedAnnotations().size() < 2)
    {
        return;
    }

    using Alignment = PDFWidgetAnnotationManager::Alignment;
    Alignment alignment = Alignment::Left;
    switch (id)
    {
        case 0: alignment = Alignment::Left; break;
        case 1: alignment = Alignment::HorizontalCenter; break;
        case 2: alignment = Alignment::Right; break;
        case 3: alignment = Alignment::Top; break;
        case 4: alignment = Alignment::VerticalCenter; break;
        case 5: alignment = Alignment::Bottom; break;
        default: return;
    }

    // T14: the anchor is the annotation, which was selected as the last one. If
    // the selection is not ordered or contains a single annotation only, the new
    // method degrades to the previous frame alignment.
    m_annotationManager->alignSelectedAnnotationsToAnchor(alignment);
}

void PDFContextualFormatPanel::onArrangeDistribute(int id, bool elementSide)
{
    if (m_isUpdatingFromModel)
    {
        return;
    }

    // No equal gap operation exists for page content elements, the buttons of the
    // element side stay disabled (the guard is a second line of defense).
    if (elementSide || !m_annotationManager)
    {
        return;
    }

    if (m_annotationManager->getSelectedAnnotations().size() < 3)
    {
        return;
    }

    m_annotationManager->distributeSelectedAnnotations(id == 0 ? Qt::Horizontal : Qt::Vertical);
}

void PDFContextualFormatPanel::onFontFamilyChanged(const QFont& font)
{
    if (m_isUpdatingFromModel)
    {
        return;
    }

    QFont updated = m_textFont;
    updated.setFamily(font.family());
    applyTextPropertiesToTargets(updated, m_textColor, m_textAlignment);
    if (!isFreeTextVisualEditing())
    {
        Q_EMIT fontChanged(updated);
    }
}

void PDFContextualFormatPanel::onFontSizeChanged(double size)
{
    if (m_isUpdatingFromModel)
    {
        return;
    }

    QFont updated = m_textFont;
    if (size > 0.0)
    {
        updated.setPointSizeF(size);
    }
    applyTextPropertiesToTargets(updated, m_textColor, m_textAlignment);
    if (!isFreeTextVisualEditing())
    {
        Q_EMIT fontChanged(updated);
    }
}

void PDFContextualFormatPanel::onTextColorButtonClicked()
{
    if (!m_inlinePalette)
    {
        return;
    }

    const QPoint globalPosition = m_textColorButton->mapToGlobal(
        QPoint(0, m_textColorButton->height()));
    m_inlinePalette->move(globalPosition);
    m_inlinePalette->show();
}

void PDFContextualFormatPanel::onBoldToggled(bool checked)
{
    if (m_isUpdatingFromModel)
    {
        return;
    }
    QFont updated = m_textFont;
    updated.setBold(checked);
    applyTextPropertiesToTargets(updated, m_textColor, m_textAlignment);
    if (!isFreeTextVisualEditing())
    {
        Q_EMIT fontChanged(updated);
    }
}

void PDFContextualFormatPanel::onItalicToggled(bool checked)
{
    if (m_isUpdatingFromModel)
    {
        return;
    }
    QFont updated = m_textFont;
    updated.setItalic(checked);
    applyTextPropertiesToTargets(updated, m_textColor, m_textAlignment);
    if (!isFreeTextVisualEditing())
    {
        Q_EMIT fontChanged(updated);
    }
}

void PDFContextualFormatPanel::onUnderlineToggled(bool checked)
{
    if (m_isUpdatingFromModel)
    {
        return;
    }
    QFont updated = m_textFont;
    updated.setUnderline(checked);
    applyTextPropertiesToTargets(updated, m_textColor, m_textAlignment);
    if (!isFreeTextVisualEditing())
    {
        Q_EMIT fontChanged(updated);
    }
}

void PDFContextualFormatPanel::onStrikeOutToggled(bool checked)
{
    if (m_isUpdatingFromModel)
    {
        return;
    }
    QFont updated = m_textFont;
    updated.setStrikeOut(checked);
    applyTextPropertiesToTargets(updated, m_textColor, m_textAlignment);
    // A free text annotation keeps its style in PDFFreeTextStyle, which has no
    // strikeout member, so the flag only reaches text box elements here.
    if (!isFreeTextVisualEditing())
    {
        Q_EMIT fontChanged(updated);
    }
}

void PDFContextualFormatPanel::onTextAlignmentClicked(int id)
{
    if (m_isUpdatingFromModel)
    {
        return;
    }

    Qt::Alignment alignment = Qt::AlignLeft;
    switch (id)
    {
        case 0: alignment = Qt::AlignLeft; break;
        case 1: alignment = Qt::AlignHCenter; break;
        case 2: alignment = Qt::AlignRight; break;
        case 3: alignment = Qt::AlignJustify; break;
        default: return;
    }

    applyTextPropertiesToTargets(m_textFont, m_textColor, alignment);
    if (!isFreeTextVisualEditing())
    {
        Q_EMIT alignmentChanged(alignment);
    }
}

void PDFContextualFormatPanel::onFreeTextEdited()
{
    if (m_isUpdatingFromModel || !m_freeTextCommitTimer)
    {
        return;
    }

    // The user really changed the text, so it may be written back. This flag
    // is set before the timer restarts - a commit triggered from another place
    // (focus out, card switch) must already see the change.
    m_freeTextDirty = true;

    // Restarting a single shot timer debounces the typing into one write back,
    // so the appearance stream is not rebuilt for every character.
    m_freeTextCommitTimer->start();
}

void PDFContextualFormatPanel::commitFreeTextContents()
{
    if (m_freeTextCommitTimer)
    {
        m_freeTextCommitTimer->stop();
    }

    if (m_isUpdatingFromModel || !m_annotationManager || !m_freeTextEdit)
    {
        return;
    }

    // Only text really typed by the user is written back. A mixed selection
    // (the editor is empty and shows the "Mixed" placeholder) or an untouched
    // editor must never clear the text of the selected annotations.
    if (!m_freeTextDirty)
    {
        return;
    }

    // The write back target is gone - the pending text is dropped instead of
    // being written into something else.
    if (!m_freeTextEdit->isEnabled())
    {
        m_freeTextDirty = false;
        return;
    }

    // The selection is not a free text annotation anymore.
    if (!m_annotationManager->hasSelectedFreeTextAnnotations())
    {
        m_freeTextDirty = false;
        return;
    }

    // The selection changed while the debounce timer was running (selection
    // change without a focus change: pasting annotations, creating a free text
    // annotation, undo / redo). The pending text belongs to the previous
    // annotation, so it must not be written into the new one.
    if (currentAnnotationAnchorSignature() != m_freeTextAnchorSignature)
    {
        m_freeTextDirty = false;
        return;
    }

    const QString text = m_freeTextEdit->toPlainText();

    // The flag is cleared before the document is touched, so a signal emitted
    // from the modification (which may reach this slot again) cannot repeat the
    // write back, and a failed write never keeps the editor dirty.
    m_freeTextDirty = false;

    if (!m_annotationManager->setSelectedFreeTextContents(text))
    {
        return;
    }

    // The values are not read back here on purpose: the editor already shows
    // the new text and a refresh would move its cursor.
}

void PDFContextualFormatPanel::onInlineTextEdited()
{
    if (m_isUpdatingFromModel || !m_inlineTextEdit)
    {
        return;
    }

    refreshFreeTextToolCache();

    const QString text = m_inlineTextEdit->toPlainText();
    if (isFreeTextVisualEditing())
    {
        // Page preview of the FreeText annotation being created.
        m_freeTextTool->setVisualText(text);
    }
    else
    {
        // Existing page content element (EditorPlugin performs the write back).
        Q_EMIT inlineTextChanged(text);
    }
}

}   // namespace pdf

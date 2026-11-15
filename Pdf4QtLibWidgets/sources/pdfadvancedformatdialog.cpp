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

#include "pdfadvancedformatdialog.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace pdf
{

PDFAdvancedFormatDialog::PDFAdvancedFormatDialog(QWidget* parent) :
    BaseClass(parent),
    m_borderWidthSpinBox(nullptr),
    m_scaleSpinBox(nullptr),
    m_authorEdit(nullptr),
    m_hintLabel(nullptr)
{
    setWindowTitle(tr("高级格式"));
    // Modeless by construction: the panel shows the dialog and the user keeps
    // working on the page. A blocking application modal call is never used for
    // this entry (SAC-10).
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose, true);
    resize(360, 220);

    createUi();
}

PDFAdvancedFormatDialog::~PDFAdvancedFormatDialog()
{

}

void PDFAdvancedFormatDialog::createUi()
{
    QVBoxLayout* rootLayout = new QVBoxLayout(this);

    QFormLayout* formLayout = new QFormLayout();
    formLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    // Border width of the selected annotations. The spin box is tracking free, so
    // a typed value is committed once when the user is done, which keeps the undo
    // stack of the annotation write back short.
    m_borderWidthSpinBox = new QDoubleSpinBox(this);
    m_borderWidthSpinBox->setObjectName(QStringLiteral("advancedBorderWidth"));
    m_borderWidthSpinBox->setRange(0.0, 20.0);
    m_borderWidthSpinBox->setDecimals(1);
    m_borderWidthSpinBox->setSingleStep(0.5);
    m_borderWidthSpinBox->setKeyboardTracking(false);
    m_borderWidthSpinBox->setToolTip(tr("选中标注的边框线宽"));
    formLayout->addRow(tr("边框线宽"), m_borderWidthSpinBox);

    // The author is displayed for reference only, see the class comment.
    m_authorEdit = new QLineEdit(this);
    m_authorEdit->setObjectName(QStringLiteral("advancedAuthor"));
    m_authorEdit->setReadOnly(true);
    m_authorEdit->setEnabled(false);
    m_authorEdit->setToolTip(tr("当前版本不支持修改标注作者"));
    formLayout->addRow(tr("作者"), m_authorEdit);

    // Relative scaling of the selected text. The value is consumed as a one shot
    // increment and the control returns to 100 %, so repeated entries never
    // accumulate (same convention as the arrange rotation angle).
    m_scaleSpinBox = new QDoubleSpinBox(this);
    m_scaleSpinBox->setObjectName(QStringLiteral("advancedTextScale"));
    m_scaleSpinBox->setRange(10.0, 400.0);
    m_scaleSpinBox->setDecimals(1);
    m_scaleSpinBox->setSingleStep(5.0);
    m_scaleSpinBox->setValue(100.0);
    m_scaleSpinBox->setSuffix(QStringLiteral(" %"));
    m_scaleSpinBox->setKeyboardTracking(false);
    m_scaleSpinBox->setToolTip(tr("相对于当前字号的缩放，回车后生效一次并回到 100%"));
    formLayout->addRow(tr("精确缩放"), m_scaleSpinBox);

    rootLayout->addLayout(formLayout);

    m_hintLabel = new QLabel(tr("低频属性入口。颜色、字号、对齐等常用属性请直接使用右侧面板，"
                                "此窗口不会阻塞页面操作。"), this);
    m_hintLabel->setWordWrap(true);
    rootLayout->addWidget(m_hintLabel);
    rootLayout->addStretch(1);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch(1);
    QPushButton* closeButton = new QPushButton(tr("关闭"), this);
    closeButton->setObjectName(QStringLiteral("advancedClose"));
    buttonLayout->addWidget(closeButton);
    rootLayout->addLayout(buttonLayout);

    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
    connect(m_borderWidthSpinBox, &QDoubleSpinBox::valueChanged,
            this, &PDFAdvancedFormatDialog::borderWidthChanged);
    connect(m_scaleSpinBox, &QDoubleSpinBox::valueChanged,
            this, [this](double value)
    {
        if (!m_scaleSpinBox || qFuzzyCompare(value, 100.0))
        {
            return;
        }
        const qreal percent = value;
        {
            // Reset before the emission, so the receiver never sees a stale value
            // and two consecutive entries produce two independent scalings.
            const QSignalBlocker blocker(m_scaleSpinBox);
            m_scaleSpinBox->setValue(100.0);
        }
        Q_EMIT scalePercentRequested(percent);
    });
}

void PDFAdvancedFormatDialog::setValues(bool annotationSelected,
                                        qreal borderWidth,
                                        const QString& author,
                                        bool textSelected)
{
    m_borderWidthSpinBox->setEnabled(annotationSelected);
    m_borderWidthSpinBox->setValue(annotationSelected ? borderWidth : 0.0);
    m_borderWidthSpinBox->setToolTip(annotationSelected ? tr("选中标注的边框线宽")
                                                        : tr("请先选中标注"));

    m_authorEdit->setText(author);

    m_scaleSpinBox->setEnabled(textSelected);
    m_scaleSpinBox->setToolTip(textSelected ? tr("相对于当前字号的缩放，回车后生效一次并回到 100%")
                                            : tr("请先选中文本"));
}

}   // namespace pdf

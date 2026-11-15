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

#ifndef PDFADVANCEDFORMATDIALOG_H
#define PDFADVANCEDFORMATDIALOG_H

#include "pdfwidgetsglobal.h"

#include <QDialog>
#include <QString>

class QLabel;
class QLineEdit;
class QDoubleSpinBox;

namespace pdf
{

/// Modeless dialog with the low frequency properties of the context format panel
/// (R-21). It is a secondary entry point: every P0 property stays available in
/// the panel itself, the dialog only carries values that do not fit the compact
/// cards, namely the exact border width of the selected annotations and an exact
/// scaling of the selected text.
///
/// The dialog is never opened with a blocking application modal call. It is
/// created once by the panel, shown with show(), and it deletes itself when the
/// user closes it (Qt::WA_DeleteOnClose). It never reads the selection on its own: the panel
/// fills in the values with setValues() right before showing it, and the dialog
/// only reports user input through its signals.
///
/// The author field is intentionally read only. PDF4QT has no public API to write
/// the /T entry of an annotation dictionary, so the value is shown for reference
/// and the control stays disabled with an explanatory tooltip (known limitation,
/// registered together with G-11 / KI-10).
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFAdvancedFormatDialog : public QDialog
{
    Q_OBJECT

private:
    using BaseClass = QDialog;

public:
    explicit PDFAdvancedFormatDialog(QWidget* parent = nullptr);
    virtual ~PDFAdvancedFormatDialog() override;

    /// Fills the controls with the values of the current selection. The dialog
    /// does not keep a copy of the selection, so the values have to be pushed
    /// again every time it is shown.
    /// \param annotationSelected True, if annotations are selected.
    /// \param borderWidth Border width of the selected annotations.
    /// \param author Author of the selected annotation, shown read only.
    /// \param textSelected True, if text elements are selected.
    void setValues(bool annotationSelected,
                   qreal borderWidth,
                   const QString& author,
                   bool textSelected);

signals:
    /// Emitted while the user drags the border width spin box. The receiver is
    /// expected to write the value to the selected annotations.
    void borderWidthChanged(qreal width);

    /// Emitted when the user commits a relative scale of the selected text. The
    /// value is a percentage of the current font size; the spin box returns to
    /// 100 % right after the emission, so two consecutive entries do not
    /// accumulate.
    void scalePercentRequested(qreal percent);

private:
    void createUi();

    QDoubleSpinBox* m_borderWidthSpinBox;
    QDoubleSpinBox* m_scaleSpinBox;
    QLineEdit* m_authorEdit;
    QLabel* m_hintLabel;
};

}   // namespace pdf

#endif // PDFADVANCEDFORMATDIALOG_H

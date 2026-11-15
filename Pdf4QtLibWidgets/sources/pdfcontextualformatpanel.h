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

#ifndef PDFCONTEXTUALFORMATPANEL_H
#define PDFCONTEXTUALFORMATPANEL_H

#include "pdfwidgetsglobal.h"
#include "pdfglobal.h"

#include <QDockWidget>
#include <QPen>
#include <QBrush>
#include <QFont>
#include <QColor>
#include <QString>
#include <QHash>
#include <QList>
#include <QPointer>

class QStackedWidget;
class QSignalBlocker;
class QSlider;
class QSpinBox;
class QToolButton;
class QTextEdit;
class QPlainTextEdit;
class QLabel;
class QButtonGroup;
class QFontComboBox;
class QDoubleSpinBox;
class QCheckBox;
class QTimer;
class QWidget;
class QEvent;
class QShowEvent;
class QHideEvent;
class FormatPreviewWidget;

namespace pdf
{
class PDFWidget;
class PDFWidgetTool;
class PDFWidgetAnnotationManager;
class PDFPageContentScene;
class PDFPageContentElementTextBox;
class PDFCreateFreeTextTool;
class PDFAdvancedFormatDialog;

/// Persistent, context sensitive "Format" dock on the right side of the editor.
///
/// The panel is created and owned by PDFEditorMainWindow (Qt parent ownership).
/// It never owns PDFWidget, PDFWidgetAnnotationManager or PDFPageContentScene;
/// the page content scene is registered by EditorPlugin while the plugin is
/// active and must be unregistered before the plugin destroys its scene.
///
/// Three cards share one QStackedWidget:
///  - Annotation: Preview / Text / Highlight Style / Opacity / Arrange
///  - Text:       Text Style / paragraph (disabled placeholders) / advanced
///  - Empty:      hint + collapsed "Defaults" group
///
/// Every write back is performed either by an existing
/// PDFWidgetAnnotationManager::setSelectedAnnotations*() method (annotation
/// side), or by signals consumed by EditorPlugin (page content element side).
/// The panel never edits a PDF document directly and never opens a modal
/// dialog.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFContextualFormatPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit PDFContextualFormatPanel(PDFWidget* widget, QWidget* parent = nullptr);
    virtual ~PDFContextualFormatPanel() override;

    /// Registers a page content scene owned by EditorPlugin. Passing nullptr or
    /// calling unregisterPageContentScene() with the same pointer disconnects it.
    void registerPageContentScene(PDFPageContentScene* scene);
    void unregisterPageContentScene(PDFPageContentScene* scene);

    /// Re-reads the current selection into the controls. All signals emitted
    /// by the controls are blocked while the values are being restored.
    void refreshFromSelection();

signals:
    /// Same names as PDFPageContentEditorWidget, so EditorPlugin can reuse its
    /// existing onPenChanged / ... slots without any change of signature.
    void penChanged(const QPen& pen);
    void brushChanged(const QBrush& brush);
    void fontChanged(const QFont& font);
    void alignmentChanged(Qt::Alignment alignment);
    void textAngleChanged(pdf::PDFReal angle);

    /// Boundaries of one continuous interaction. They are emitted around one
    /// inline text editing session (focus in / focus out of the inline editor
    /// and card switches), so that the whole session becomes a single undo
    /// step of the editor plugin.
    void continuousEditStarted();
    void continuousEditFinished();

    /// Text typed into the inline, modeless QTextEdit of the text card.
    void inlineTextChanged(const QString& text);

    /// Emitted when the user clicks "set as default" on the text card (R-20).
    /// The current values are passed explicitly: the plugin has no access to the
    /// panel state, and the selected object must not be modified (AC-14), which
    /// is why the existing fontChanged / penChanged signals cannot be reused.
    void setAsDefaultRequested(const QFont& font, const QColor& color, Qt::Alignment alignment);

    /// Emitted when the user clicks "set as default" on the annotation card.
    /// Only the colour is promoted, the annotation style has no opacity member
    /// (known limitation KI-06 / G-14).
    void annotationDefaultStyleRequested(const QColor& color);

protected:
    virtual void showEvent(QShowEvent* event) override;
    virtual void hideEvent(QHideEvent* event) override;

    /// Watches the focus events of the inline text editor, which delimit one
    /// undo transaction of the editor plugin.
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class Card { Annotation, Text, Empty };

    /// One instance of the "Arrange" group (R-16). The group exists twice: in
    /// the annotation card (the targets are annotations) and in the text card
    /// (the targets are page content elements), so every control is stored per
    /// instance. The element side supports Page Center / Rotate / Alignment, its
    /// Flip and Distribute stay disabled (see the tooltips and G-9 / KI-05).
    struct ArrangeControls
    {
        bool elementSide = false;
        QToolButton* pageCenter = nullptr;
        QToolButton* rotateClockwise = nullptr;
        QToolButton* rotateCounterClockwise = nullptr;
        QDoubleSpinBox* rotateAngle = nullptr;
        QToolButton* flipHorizontal = nullptr;
        QToolButton* flipVertical = nullptr;
        QList<QToolButton*> alignButtons;
        QList<QToolButton*> distributeButtons;
    };

    QWidget* createAnnotationCard();
    QWidget* createTextCard();
    QWidget* createEmptyCard();
    QWidget* createArrangeGroup(QWidget* parent, ArrangeControls& controls, bool elementSide);
    QWidget* createCollapsibleGroup(const QString& title,
                                   const QString& key,
                                   QWidget* content,
                                   bool defaultExpanded);
    QWidget* createInlineColorPalette();

    /// Creates the footer row of a card with the "set as default" button (R-20).
    /// The button is disabled until an object of the matching kind is selected.
    /// \param defaultButton Receives the created button.
    QWidget* createCardFooterRow(QToolButton** defaultButton);
    QToolButton* createColorSwatchButton(const QColor& color,
                                         const QString& toolTip,
                                         bool enabled);
    QToolButton* createDisabledPlaceholder(const QString& text);

    void scheduleStateUpdate();
    void applyState();
    void setCard(Card card);

    /// Identifies the displayed selection (annotation references, scene element
    /// identifier and the FreeText preview state).
    QString buildSelectionSignature() const;

    /// True, if the current scene selection contains at least one text box.
    bool hasTextBoxSelection() const;
    void clearCardValues();
    void readAnnotationValues();
    void readTextElementValues();
    void updatePreview();
    /// Re-computes the availability of both Arrange groups from the current
    /// selection (annotations -> annotation card, page content elements -> text
    /// card). The thresholds are: Page Center / Rotate / Flip >= 1, Alignment
    /// >= 2, Distribute >= 3.
    void updateArrangeAvailability();
    /// Enables the "set as default" button of a card only while an object of the
    /// matching kind is selected (R-20 / AC-14).
    void updateSetAsDefaultAvailability();
    /// Enables / disables every control of one Arrange group.
    /// \param controls Group to update
    /// \param count Number of selected objects of that side
    /// \param rotateSupported True, if the targets can be rotated
    /// \param flipSupported True, if the targets can be flipped
    void updateArrangeGroup(const ArrangeControls& controls,
                            int count,
                            bool rotateSupported,
                            bool flipSupported);
    /// True, if the selection of the page content scene is not empty and contains
    /// text boxes only (the only elements, which can be rotated).
    bool hasTextOnlyElementSelection(int* count) const;
    /// Absolute text angle of the first selected text box (0 without selection).
    pdf::PDFReal selectedTextAngle() const;
    /// Rotates the targets of one side by a relative angle.
    void rotateTargets(bool elementSide, pdf::PDFReal deltaDegrees);
    void updateTextControlsFromValues(const QFont& font,
                                      const QColor& color,
                                      Qt::Alignment alignment,
                                      bool applyDecorationState);
    void applyTextPropertiesToTargets(const QFont& font,
                                      const QColor& color,
                                      Qt::Alignment alignment);
    bool isFreeTextVisualEditing() const;
    bool isTextToolActive();

    /// Re-reads the active tool and caches it as the FreeText tool.
    void refreshFreeTextToolCache();

    /// Returns a stable identity of the current annotation selection, built
    /// from the object references of the selected annotations, sorted by
    /// object number and generation ("num:gen,num:gen"). Returns an empty
    /// string if there is no selection. Used to decide whether a pending write
    /// back of the free text editor still belongs to the selected annotation.
    /// Read only, emits nothing.
    QString currentAnnotationAnchorSignature() const;

    /// Opens / closes the undo transaction of the inline text editor. Both are
    /// idempotent, the closing one is also called as a fallback whenever the
    /// inline editor can lose the focus without a FocusOut event.
    void beginInlineEditTransaction();
    void finishInlineEditTransaction();

    void onAnnotationSelectionChanged();
    void onSceneSelectionChanged();
    void onSceneChanged(bool graphicsOnly);
    void onActiveToolChanged();
    void onToolPollTimeout();
    void onColorButtonClicked(int index);
    void onOpacityChanged(int value);
    void onOpacitySpinBoxChanged(int value);
    void onInlineColorSelected(const QColor& color);
    void onArrangePageCenter(bool elementSide);
    void onArrangeRotate(bool elementSide, int direction);
    void onArrangeRotateAngle(bool elementSide, double deltaDegrees);
    void onArrangeFlip(bool elementSide, Qt::Orientation orientation);
    void onArrangeAlignment(int id, bool elementSide);
    void onArrangeDistribute(int id, bool elementSide);
    void onFontFamilyChanged(const QFont& font);
    void onFontSizeChanged(double size);
    void onTextColorButtonClicked();
    void onBoldToggled(bool checked);
    void onItalicToggled(bool checked);
    void onUnderlineToggled(bool checked);
    void onStrikeOutToggled(bool checked);
    void onTextAlignmentClicked(int id);
    void onInlineTextEdited();
    void onSetAsDefaultClicked();

    /// Opens the modeless low frequency dialog (R-21). The dialog is created once
    /// and shown with show(); it is never opened with exec() (SAC-10).
    void openAdvancedDialog();
    /// Writes the exact border width of the advanced dialog to the selection.
    void onAdvancedBorderWidthChanged(qreal width);
    /// Scales the selected text by a relative percentage (one shot increment).
    void onAdvancedScalePercentRequested(qreal percent);

    /// R-22: locks / unlocks the selected annotations. The write back goes
    /// through the dedicated manager path, which bypasses the
    /// canModifyAnnotation() filter (otherwise unlocking would be a no-op).
    void onLockedToggled(bool checked);

    /// Debounced write back of the free text annotation editor.
    void onFreeTextEdited();
    void commitFreeTextContents();

    PDFWidget* m_widget;                                  ///< Not owned.
    PDFWidgetAnnotationManager* m_annotationManager;    ///< Not owned.
    QPointer<PDFPageContentScene> m_scene;              ///< Not owned, registered by the plugin.
    QPointer<PDFCreateFreeTextTool> m_freeTextTool;     ///< Not owned, refreshed before every use.
    QPointer<PDFWidgetTool> m_lastActiveTool;           ///< Not owned, polling cache.

    QStackedWidget* m_stack;
    QWidget* m_annotationCard;
    QWidget* m_textCard;
    QWidget* m_emptyCard;
    QWidget* m_inlinePalette;
    FormatPreviewWidget* m_preview;
    QTimer* m_toolPollTimer;

    QHash<QString, bool> m_groupExpanded;
    Card m_card;

    /// Signature of the selection which is currently shown in the controls.
    /// The controls are re-read only when this signature or the card changes.
    QString m_lastSelectionSignature;
    bool m_isUpdatingFromModel;
    bool m_stateUpdatePending;

    /// Cached result of the FreeText visual editing state, used by the tool
    /// poll (a state change of the tool has no signal of its own).
    bool m_lastFreeTextEditingState;

    /// True between continuousEditStarted() and continuousEditFinished() of the
    /// inline text editor.
    bool m_inlineEditInTransaction;

    /// True, if the user really changed the text of the free text editor since
    /// the last backfill. Only a dirty editor is written back, so a selection
    /// change (or an undo) can never write stale text into another annotation.
    bool m_freeTextDirty;

    /// Identity of the annotation selection at the moment the free text editor
    /// was filled or last written back (see
    /// currentAnnotationAnchorSignature()). A write back is performed only when
    /// the selection is still the same.
    QString m_freeTextAnchorSignature;

    // Annotation card controls
    QList<QToolButton*> m_colorButtons;
    QButtonGroup* m_colorButtonGroup;
    QSlider* m_opacitySlider;
    QSpinBox* m_opacitySpinBox;
    QLabel* m_mixedHintLabel;
    QPlainTextEdit* m_freeTextEdit;
    QTimer* m_freeTextCommitTimer;
    /// Arrange group of the annotation card (targets annotations).
    ArrangeControls m_annotationArrange;
    /// Arrange group of the text card (targets page content elements).
    ArrangeControls m_textArrange;
    QColor m_annotationColor;
    int m_annotationOpacity;                            ///< 0 - 100
    bool m_annotationColorMixed;
    bool m_annotationOpacityMixed;
    QList<QColor> m_colorPalette;

    // Text card controls
    QFontComboBox* m_fontCombo;
    QDoubleSpinBox* m_fontSizeSpinBox;
    QToolButton* m_textColorButton;
    QToolButton* m_boldButton;
    QToolButton* m_italicButton;
    QToolButton* m_underlineButton;
    QToolButton* m_strikeOutButton;
    /// "Set as default" buttons (R-20), one per card.
    QToolButton* m_annotationSetAsDefaultButton;
    QToolButton* m_textSetAsDefaultButton;
    /// Modeless advanced dialog (R-21). QPointer, because the dialog deletes
    /// itself on close (Qt::WA_DeleteOnClose).
    QPointer<PDFAdvancedFormatDialog> m_advancedDialog;
    /// Annotation lock checkbox (R-22, P2). Tristate: checked / unchecked /
    /// partially checked for a mixed selection.
    QCheckBox* m_lockedCheckBox;
    QButtonGroup* m_textAlignmentGroup;
    QList<QToolButton*> m_textAlignmentButtons;
    QTextEdit* m_inlineTextEdit;
    QFont m_textFont;
    QColor m_textColor;
    Qt::Alignment m_textAlignment;
};

}   // namespace pdf

#endif // PDFCONTEXTUALFORMATPANEL_H

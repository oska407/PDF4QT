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

#include "editorplugin.h"
#include "pdfcontextualformatpanel.h"
#include "pdfdrawwidget.h"
#include "pdfutils.h"
#include "pdfpagecontenteditorwidget.h"
#include "pdfpagecontenteditorstylesettings.h"
#include "pdfdocumentbuilder.h"
#include "pdfcertificatemanagerdialog.h"
#include "pdfdocumentwriter.h"
#include "pdfpagecontenteditorprocessor.h"
#include "pdfpagecontenteditortools.h"
#include "pdfwidgettool.h"
#include "pdfannotationstyle.h"
#include "pdfadvancedtools.h"
#include "pdfpagecontenteditorcontentstreambuilder.h"
#include "pdfstreamfilters.h"
#include "pdfoptimizer.h"

#include <QAction>
#include <QToolButton>
#include <QMainWindow>
#include <QWidget>
#include <QMessageBox>
#include <QFileDialog>
#include <QSettings>
#include <QCoreApplication>
#include <memory>

namespace pdfplugin
{

EditorPlugin::EditorPlugin() :
    pdf::PDFPlugin(nullptr),
    m_actions({ }),
    m_tools({ }),
    m_editorWidget(nullptr),
    m_scene(nullptr),
    m_sceneSelectionChangeEnabled(true),
    m_isSaving(false),
    m_isUndoRedoInProgress(false)
{
    m_scene.setIsPageContentDrawSuppressed(true);
}

void EditorPlugin::setWidget(pdf::PDFWidget* widget)
{
    Q_ASSERT(!m_widget);

    BaseClass::setWidget(widget);

    QAction* activateAction = new QAction(QIcon(":/pdfplugins/editorplugin/activate.svg"), tr("&Edit page content"), this);
    QAction* createTextAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-text.svg"), tr("Create &Text Label"), this);
    QAction* createFreehandCurveAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-freehand-curve.svg"), tr("Create &Freehand Curve"), this);
    QAction* createAcceptMarkAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-yes-mark.svg"), tr("Create &Accept Mark"), this);
    QAction* createRejectMarkAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-no-mark.svg"), tr("Create &Reject Mark"), this);
    QAction* createRectangleAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-rectangle.svg"), tr("Create R&ectangle"), this);
    QAction* createRoundedRectangleAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-rounded-rectangle.svg"), tr("&Create Rounded Rectangle"), this);
    QAction* createHorizontalLineAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-horizontal-line.svg"), tr("Create &Horizontal Line"), this);
    QAction* createVerticalLineAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-vertical-line.svg"), tr("Create &Vertical Line"), this);
    QAction* createLineAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-line.svg"), tr("Create L&ine"), this);
    QAction* createDotAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-dot.svg"), tr("Create &Dot"), this);
    QAction* createSvgImageAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-svg-image.svg"), tr("Create &SVG Image"), this);
    QAction* createMultipleElementsAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-multiple.svg"), tr("Create &Multiple Elements"), this);
    QAction* undoAction = new QAction(QIcon(":/resources/undo.svg"), tr("&Undo"), this);
    QAction* redoAction = new QAction(QIcon(":/resources/redo.svg"), tr("&Redo"), this);
    QAction* clearAction = new QAction(QIcon(":/pdfplugins/editorplugin/clear.svg"), tr("Clear A&ll Graphics"), this);

    activateAction->setObjectName("editortool_activateAction");
    createTextAction->setObjectName("editortool_createTextAction");
    createFreehandCurveAction->setObjectName("editortool_createFreehandCurveAction");
    createAcceptMarkAction->setObjectName("editortool_createAcceptMarkAction");
    createRejectMarkAction->setObjectName("editortool_createRejectMarkAction");
    createRectangleAction->setObjectName("editortool_createRectangleAction");
    createRoundedRectangleAction->setObjectName("editortool_createRoundedRectangleAction");
    createHorizontalLineAction->setObjectName("editortool_createHorizontalLineAction");
    createVerticalLineAction->setObjectName("editortool_createVerticalLineAction");
    createLineAction->setObjectName("editortool_createLineAction");
    createDotAction->setObjectName("editortool_createDotAction");
    createSvgImageAction->setObjectName("editortool_createSvgImageAction");
    createMultipleElementsAction->setObjectName("editortool_createMultipleElementsAction");
    undoAction->setObjectName("editortool_undoAction");
    redoAction->setObjectName("editortool_redoAction");
    clearAction->setObjectName("editortool_clearAction");

    activateAction->setCheckable(true);
    createTextAction->setCheckable(true);
    createFreehandCurveAction->setCheckable(true);
    createAcceptMarkAction->setCheckable(true);
    createRejectMarkAction->setCheckable(true);
    createRectangleAction->setCheckable(true);
    createRoundedRectangleAction->setCheckable(true);
    createHorizontalLineAction->setCheckable(true);
    createVerticalLineAction->setCheckable(true);
    createLineAction->setCheckable(true);
    createDotAction->setCheckable(true);
    createSvgImageAction->setCheckable(true);
    createMultipleElementsAction->setCheckable(true);
    createMultipleElementsAction->setToolTip(tr("Create Multiple Elements\n\n"
                                                "The creation tool stays active after an element has been created "
                                                "and the size of the last created element is reused, so a single click "
                                                "creates the next element of the same size. "
                                                "Hold Shift to define a different size."));

    m_actions[Activate] = activateAction;
    m_actions[Text] = createTextAction;
    m_actions[FreehandCurve] = createFreehandCurveAction;
    m_actions[AcceptMark] = createAcceptMarkAction;
    m_actions[RejectMark] = createRejectMarkAction;
    m_actions[Rectangle] = createRectangleAction;
    m_actions[RoundedRectangle] = createRoundedRectangleAction;
    m_actions[HorizontalLine] = createHorizontalLineAction;
    m_actions[VerticalLine] = createVerticalLineAction;
    m_actions[Line] = createLineAction;
    m_actions[Dot] = createDotAction;
    m_actions[SvgImage] = createSvgImageAction;
    m_actions[CreateMultipleElements] = createMultipleElementsAction;
    m_actions[Undo] = undoAction;
    m_actions[Redo] = redoAction;
    m_actions[Clear] = clearAction;

    undoAction->setShortcut(QKeySequence::Undo);
    redoAction->setShortcut(QKeySequence::Redo);

    // Jakub Melka: the tool actions are enabled only when the page content editing
    // is active, so single letter shortcuts do not collide with the rest of the
    // application. While a text label is being edited, the printable characters are
    // consumed by the text editor, so the shortcuts do not interfere with typing.
    activateAction->setShortcut(QKeySequence("Ctrl+Shift+E"));
    createTextAction->setShortcut(QKeySequence("T"));
    createFreehandCurveAction->setShortcut(QKeySequence("F"));
    createAcceptMarkAction->setShortcut(QKeySequence("A"));
    createRejectMarkAction->setShortcut(QKeySequence("X"));
    createRectangleAction->setShortcut(QKeySequence("R"));
    createRoundedRectangleAction->setShortcut(QKeySequence("Shift+R"));
    createHorizontalLineAction->setShortcut(QKeySequence("H"));
    createVerticalLineAction->setShortcut(QKeySequence("V"));
    createLineAction->setShortcut(QKeySequence("L"));
    createDotAction->setShortcut(QKeySequence("D"));
    createSvgImageAction->setShortcut(QKeySequence("I"));
    createMultipleElementsAction->setShortcut(QKeySequence("M"));

    QFile acceptMarkFile(":/pdfplugins/editorplugin/accept-mark.svg");
    QByteArray acceptMarkContent;
    if (acceptMarkFile.open(QFile::ReadOnly))
    {
        acceptMarkContent = acceptMarkFile.readAll();
        acceptMarkFile.close();
    }

    QFile rejectMarkFile(":/pdfplugins/editorplugin/reject-mark.svg");
    QByteArray rejectMarkContent;
    if (rejectMarkFile.open(QFile::ReadOnly))
    {
        rejectMarkContent = rejectMarkFile.readAll();
        rejectMarkFile.close();
    }

    m_tools[TextTool] = new pdf::PDFCreatePCElementTextTool(widget->getDrawWidgetProxy(), &m_scene, createTextAction, this);
    m_tools[FreehandCurveTool] = new pdf::PDFCreatePCElementFreehandCurveTool(widget->getDrawWidgetProxy(), &m_scene, createFreehandCurveAction, this);
    m_tools[AcceptMarkTool] = new pdf::PDFCreatePCElementImageTool(widget->getDrawWidgetProxy(), &m_scene, createAcceptMarkAction, acceptMarkContent, false, this);
    m_tools[RejectMarkTool] = new pdf::PDFCreatePCElementImageTool(widget->getDrawWidgetProxy(), &m_scene, createRejectMarkAction, rejectMarkContent, false, this);
    m_tools[RectangleTool] = new pdf::PDFCreatePCElementRectangleTool(widget->getDrawWidgetProxy(), &m_scene, createRectangleAction, false, this);
    m_tools[RoundedRectangleTool] = new pdf::PDFCreatePCElementRectangleTool(widget->getDrawWidgetProxy(), &m_scene, createRoundedRectangleAction, true, this);
    m_tools[HorizontalLineTool] = new pdf::PDFCreatePCElementLineTool(widget->getDrawWidgetProxy(), &m_scene, createHorizontalLineAction, true, false, this);
    m_tools[VerticalLineTool] = new pdf::PDFCreatePCElementLineTool(widget->getDrawWidgetProxy(), &m_scene, createVerticalLineAction, false, true, this);
    m_tools[LineTool] = new pdf::PDFCreatePCElementLineTool(widget->getDrawWidgetProxy(), &m_scene, createLineAction, false, false, this);
    m_tools[DotTool] = new pdf::PDFCreatePCElementDotTool(widget->getDrawWidgetProxy(), &m_scene, createDotAction, this);
    m_tools[ImageTool] = new pdf::PDFCreatePCElementImageTool(widget->getDrawWidgetProxy(), &m_scene, createSvgImageAction, QByteArray(), true, this);

    pdf::PDFToolManager* toolManager = widget->getToolManager();
    for (pdf::PDFWidgetTool* tool : m_tools)
    {
        toolManager->addTool(tool);
        connect(tool, &pdf::PDFWidgetTool::toolActivityChanged, this, &EditorPlugin::onToolActivityChanged);
    }

    m_widget->addInputInterface(&m_scene);
    m_widget->getDrawWidgetProxy()->registerDrawInterface(&m_scene);
    m_scene.setWidget(m_widget);
    connect(&m_scene, &pdf::PDFPageContentScene::sceneChanged, this, &EditorPlugin::onSceneChanged);
    connect(&m_scene, &pdf::PDFPageContentScene::selectionChanged, this, &EditorPlugin::onSceneSelectionChanged);
    connect(&m_scene, &pdf::PDFPageContentScene::editElementRequest, this, &EditorPlugin::onSceneEditElement);
    connect(clearAction, &QAction::triggered, &m_scene, &pdf::PDFPageContentScene::clear);
    connect(undoAction, &QAction::triggered, this, &EditorPlugin::onUndoTriggered);
    connect(redoAction, &QAction::triggered, this, &EditorPlugin::onRedoTriggered);
    connect(activateAction, &QAction::triggered, this, &EditorPlugin::onSetActive);
    connect(createMultipleElementsAction, &QAction::triggered, this, &EditorPlugin::onCreateMultipleElementsTriggered);
    connect(createMultipleElementsAction, &QAction::triggered, this, &EditorPlugin::writeSettings);
    connect(m_widget->getDrawWidgetProxy(), &pdf::PDFDrawWidgetProxy::drawSpaceChanged, this, &EditorPlugin::onDrawSpaceChanged);
    connect(m_widget, &pdf::PDFWidget::sceneActivityChanged, this, &EditorPlugin::onSceneActivityChanged);

    readSettings();
    updateActions();
}

void EditorPlugin::setDocument(const pdf::PDFModifiedDocument& document)
{
    BaseClass::setDocument(document);

    if (document.hasReset())
    {
        clearUndoRedo();
        setActive(false);
        updateActions();
    }
}

std::vector<QAction*> EditorPlugin::getActions() const
{
    // Jakub Melka: all the actions are offered, so the page content editing and its
    // tools can be found in the menu of the application and not only in the toolbox,
    // which is displayed when the editing is already active. A null action creates
    // a separator both in the menu and in the toolbar of the plugin.
    std::vector<QAction*> result;

    result.push_back(m_actions[Activate]);
    result.push_back(nullptr);

    for (auto actionId : { Text, FreehandCurve, AcceptMark, RejectMark,
                           Rectangle, RoundedRectangle, HorizontalLine,
                           VerticalLine, Line, Dot, SvgImage })
    {
        result.push_back(m_actions[actionId]);
    }

    result.push_back(nullptr);
    result.push_back(m_actions[CreateMultipleElements]);
    result.push_back(nullptr);
    result.push_back(m_actions[Undo]);
    result.push_back(m_actions[Redo]);
    result.push_back(m_actions[Clear]);

    return result;
}

QString EditorPlugin::getPluginMenuName() const
{
    return tr("Ed&itor");
}

pdf::PDFPlugin::PluginMenuLocation EditorPlugin::getPluginMenuLocation() const
{
    return PluginMenuLocation::Edit;
}

std::vector<QAction*> EditorPlugin::getToolbarActions() const
{
    // Jakub Melka: only the activation of the page content editing is placed on the
    // toolbar - the tools would not fit on it. They are offered in the menu of the
    // plugin and in the toolbox, which is displayed while the editing is active.
    std::vector<QAction*> result;

    result.push_back(m_actions[Activate]);

    return result;
}

bool EditorPlugin::updatePageContent(pdf::PDFInteger pageIndex,
                                     const std::vector<const pdf::PDFPageContentElement*>& elements,
                                     pdf::PDFDocumentBuilder* builder)
{
    pdf::PDFColorConvertor convertor;
    const pdf::PDFPage* page = m_document->getCatalog()->getPage(pageIndex);
    const pdf::PDFEditedPageContent& editedPageContent = m_editedPageContent.at(pageIndex);

    QRectF mediaBox = page->getMediaBox();
    QRectF mediaBoxMM = page->getMediaBoxMM();

    pdf::PDFPageContentEditorContentStreamBuilder contentStreamBuilder(m_document);
    contentStreamBuilder.setFontDictionary(editedPageContent.getFontDictionary());
    contentStreamBuilder.setXObjectDictionary(editedPageContent.getXObjectDictionary());
    contentStreamBuilder.setGraphicStateDictionary(editedPageContent.getGraphicStateDictionary());
    contentStreamBuilder.setShadingDictionary(editedPageContent.getShadingDictionary());

    for (const pdf::PDFPageContentElement* element : elements)
    {
        const pdf::PDFPageContentElementEdited* editedElement = element->asElementEdited();
        const pdf::PDFPageContentElementRectangle* elementRectangle = element->asElementRectangle();
        const pdf::PDFPageContentElementLine* elementLine = element->asElementLine();
        const pdf::PDFPageContentElementDot* elementDot = element->asElementDot();
        const pdf::PDFPageContentElementFreehandCurve* elementFreehandCurve = element->asElementFreehandCurve();
        const pdf::PDFPageContentImageElement* elementImage = element->asElementImage();
        const pdf::PDFPageContentElementTextBox* elementTextBox = element->asElementTextBox();

        if (editedElement)
        {
            contentStreamBuilder.writeEditedElement(editedElement->getElement());
        }

        if (elementRectangle)
        {
            QRectF rect = elementRectangle->getRectangle();

            QPainterPath path;
            if (elementRectangle->isRounded())
            {
                qreal radius = qMin(rect.width(), rect.height()) * 0.25;
                path.addRoundedRect(rect, radius, radius, Qt::AbsoluteSize);
            }
            else
            {
                path.addRect(rect);
            }

            const bool stroke = elementRectangle->getPen().style() != Qt::NoPen;
            const bool fill = elementRectangle->getBrush().style() != Qt::NoBrush;
            contentStreamBuilder.writeStyledPath(path, elementRectangle->getPen(), elementRectangle->getBrush(), stroke, fill);
        }

        if (elementLine)
        {
            QLineF line = elementLine->getLine();
            QPainterPath path;
            path.moveTo(line.p1());
            path.lineTo(line.p2());

            contentStreamBuilder.writeStyledPath(path, elementLine->getPen(), elementLine->getBrush(), true, false);
        }

        if (elementDot)
        {
            QPen pen = elementDot->getPen();
            const qreal radius = pen.widthF() * 0.5;

            QPainterPath path;
            path.addEllipse(elementDot->getPoint(), radius, radius);

            contentStreamBuilder.writeStyledPath(path, Qt::NoPen, QBrush(pen.color()), false, true);
        }

        if (elementFreehandCurve)
        {
            QPainterPath path = elementFreehandCurve->getCurve();
            contentStreamBuilder.writeStyledPath(path, elementFreehandCurve->getPen(), elementFreehandCurve->getBrush(), true, false);
        }

        if (elementImage)
        {
            QImage image = elementImage->getImage();
            if (!image.isNull())
            {
                contentStreamBuilder.writeImage(image, elementImage->getRectangle());
            }
            else
            {
                // It is probably an SVG image
                pdf::PDFContentEditorPaintDevice paintDevice(&contentStreamBuilder, mediaBox, mediaBoxMM);
                QPainter painter(&paintDevice);

                QList<pdf::PDFRenderError> errors;
                pdf::PDFTextLayoutGetter textLayoutGetter(nullptr, pageIndex);
                elementImage->drawPage(&painter, &m_scene, pageIndex, nullptr, textLayoutGetter, QTransform(), convertor, errors);
            }
        }

        if (elementTextBox)
        {
            pdf::PDFContentEditorPaintDevice paintDevice(&contentStreamBuilder, mediaBox, mediaBoxMM);
            QPainter painter(&paintDevice);

            QList<pdf::PDFRenderError> errors;
            pdf::PDFTextLayoutGetter textLayoutGetter(nullptr, pageIndex);
            elementTextBox->drawPage(&painter, &m_scene, pageIndex, nullptr, textLayoutGetter, QTransform(), convertor, errors);
        }
    }

    QStringList errors = contentStreamBuilder.getErrors();
    contentStreamBuilder.clearErrors();

    if (!errors.empty())
    {
        const int errorCount = errors.size();
        if (errors.size() > 3)
        {
            errors.resize(3);
        }

        QString message = tr("Errors (%2) occured while creating content stream on page %3.<br>%1").arg(errors.join("<br>")).arg(errorCount).arg(pageIndex + 1);
        if (QMessageBox::question(m_dataExchangeInterface->getMainWindow(), tr("Error"), message, QMessageBox::Abort, QMessageBox::Ignore) == QMessageBox::Abort)
        {
            return false;
        }
    }

    pdf::PDFDictionaryBuilder fontDictionary = contentStreamBuilder.getFontDictionary();
    pdf::PDFDictionaryBuilder xobjectDictionary = contentStreamBuilder.getXObjectDictionary();
    pdf::PDFDictionaryBuilder graphicStateDictionary = contentStreamBuilder.getGraphicStateDictionary();
    pdf::PDFDictionaryBuilder shadingDictionary = contentStreamBuilder.getShadingDictionary();

    builder->replaceObjectsByReferences(fontDictionary);
    builder->replaceObjectsByReferences(xobjectDictionary);
    builder->replaceObjectsByReferences(graphicStateDictionary);
    builder->replaceObjectsByReferences(shadingDictionary);

    pdf::PDFArrayBuilder array;
    array.appendItem(pdf::PDFObject::createName("FlateDecode"));

    // Compress the content stream
    QByteArray compressedData = pdf::PDFFlateDecodeFilter::compress(contentStreamBuilder.getOutputContent());
    pdf::PDFDictionaryBuilder contentDictionary;
    contentDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(compressedData.size()));
    contentDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Filter"), pdf::PDFObject::createArray(qMove(array)));
    pdf::PDFObject contentObject = pdf::PDFObject::createStream(pdf::PDFStream(qMove(contentDictionary), qMove(compressedData)));

    pdf::PDFObject pageObject = builder->getObjectByReference(page->getPageReference());

    // The new resource dictionary is created from the current one (which can also
    // be inherited from the page tree), so resources, which are not regenerated by
    // the content stream builder - for example color spaces, patterns, shadings or
    // properties - are preserved. Only the regenerated categories are replaced.
    // The current resources cannot be merged by the merge operation below, because
    // they are usually an indirect object, which is replaced as a whole.
    pdf::PDFDictionaryBuilder resourcesDictionary;
    if (const pdf::PDFDictionary* currentResourcesDictionary = m_document->getDictionaryFromObject(page->getResources()))
    {
        resourcesDictionary = pdf::PDFDictionaryBuilder(*currentResourcesDictionary);
    }

    auto setResources = [&resourcesDictionary](const char* key, const pdf::PDFDictionaryBuilder& dictionary)
    {
        if (!dictionary.isEmpty())
        {
            resourcesDictionary.setEntry(pdf::PDFInplaceOrMemoryString(key),
                                         pdf::PDFObject::createDictionary(dictionary));
        }
    };

    setResources("Font", fontDictionary);
    setResources("XObject", xobjectDictionary);
    setResources("ExtGState", graphicStateDictionary);
    setResources("Shading", shadingDictionary);

    pdf::PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Resources");
    factory << resourcesDictionary;
    factory.endDictionaryItem();

    factory.beginDictionaryItem("Contents");
    factory << builder->addObject(std::move(contentObject));
    factory.endDictionaryItem();

    factory.endDictionary();

    pageObject = pdf::PDFObjectManipulator::merge(pageObject, factory.takeObject(), pdf::PDFObjectManipulator::RemoveNullObjects);
    builder->setObject(page->getPageReference(), std::move(pageObject));

    return true;
}

bool EditorPlugin::hasUnwrittenChanges() const
{
    // While the page content editing is active, the edited content is held
    // by the scene only - it is written into the document when the editing
    // is finished.
    return m_scene.isActive();
}

bool EditorPlugin::writeUnwrittenChanges()
{
    pdf::PDFTemporaryValueChange guard(&m_isSaving, true);
    return writePageContentToDocument();
}

bool EditorPlugin::save()
{
    pdf::PDFTemporaryValueChange guard(&m_isSaving, true);

    auto answer = QMessageBox::question(m_dataExchangeInterface->getMainWindow(), tr("Confirm Changes"), tr("The changes to the page content will be written to the document. Do you want to continue?"), QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Cancel);

    if (answer == QMessageBox::Cancel)
    {
        return false;
    }

    if (answer == QMessageBox::Yes)
    {
        return writePageContentToDocument();
    }

    return true;
}

bool EditorPlugin::writePageContentToDocument()
{
    pdf::PDFDocumentModifier modifier(m_document);
    pdf::PDFDocumentBuilder* builder = modifier.getBuilder();

    std::set<pdf::PDFInteger> pageIndices;
    for (const auto& item : m_editedPageContent)
    {
        pageIndices.insert(item.first);
    }

    std::map<pdf::PDFInteger, std::vector<const pdf::PDFPageContentElement*>> elementsByPage = m_scene.getElementsByPage();
    for (pdf::PDFInteger pageIndex : pageIndices)
    {
        if (m_editedPageContent.count(pageIndex) == 0)
        {
            continue;
        }

        std::vector<const pdf::PDFPageContentElement*> elements;
        auto it = elementsByPage.find(pageIndex);
        if (it != elementsByPage.cend())
        {
            elements = std::move(it->second);
        }

        if (!updatePageContent(pageIndex, elements, builder))
        {
            return false;
        }

        modifier.markReset();
    }

    pdf::PDFTemporaryValueChange restoreGuard(&m_isUndoRedoInProgress, true);
    clearUndoRedo();
    m_scene.clear();
    m_editedPageContent.clear();

    if (modifier.finalize())
    {
        pdf::PDFDocument document = *modifier.getDocument();
        pdf::PDFOptimizer optimizer(pdf::PDFOptimizer::DereferenceSimpleObjects |
                                    pdf::PDFOptimizer::RemoveNullObjects |
                                    pdf::PDFOptimizer::RemoveUnusedObjects |
                                    pdf::PDFOptimizer::MergeIdenticalObjects |
                                    pdf::PDFOptimizer::ShrinkObjectStorage, nullptr);
        optimizer.setDocument(&document);
        optimizer.optimize();
        document = optimizer.takeOptimizedDocument();

        const pdf::PDFModifiedDocument::ModificationFlags flags = modifier.getFlags() | pdf::PDFModifiedDocument::PreserveUndoRedo;
        Q_EMIT m_widget->getToolManager()->documentModified(pdf::PDFModifiedDocument(pdf::PDFDocumentPointer(new pdf::PDFDocument(std::move(document))), nullptr, flags));
    }

    return true;
}

void EditorPlugin::onSceneActivityChanged()
{
    updateActions();
}

void EditorPlugin::onSceneChanged(bool graphicsOnly)
{
    if (!graphicsOnly && !m_isUndoRedoInProgress && !m_isSaving)
    {
        createUndoStep();
    }

    if (!graphicsOnly)
    {
        updateActions();
    }

    if (m_editorWidget)
    {
        m_editorWidget->updateItemsInListWidget();
    }

    updateGraphics();
}

void EditorPlugin::onSceneSelectionChanged()
{
    if (m_editorWidget && m_sceneSelectionChangeEnabled)
    {
        m_editorWidget->setSelection(m_scene.getSelectedElementIds());
    }
}

void EditorPlugin::onWidgetSelectionChanged()
{
    Q_ASSERT(m_editorWidget);

    pdf::PDFTemporaryValueChange guard(&m_sceneSelectionChangeEnabled, false);
    m_scene.setSelectedElementIds(m_editorWidget->getSelection());
}

pdf::PDFWidgetTool* EditorPlugin::getActiveTool()
{
    for (pdf::PDFWidgetTool* currentTool : m_tools)
    {
        if (currentTool->isActive())
        {
            return currentTool;
        }
    }

    return nullptr;
}

void EditorPlugin::onToolActivityChanged()
{
    if (m_editorWidget)
    {
        pdf::PDFWidgetTool* activeTool = getActiveTool();

        const pdf::PDFPageContentElement* element = nullptr;
        pdf::PDFCreatePCElementTool* tool = qobject_cast<pdf::PDFCreatePCElementTool*>(activeTool);
        if (tool)
        {
            element = tool->getElement();
        }

        m_editorWidget->loadStyleFromElement(element);
    }
}

void EditorPlugin::onSceneEditElement(const std::set<pdf::PDFInteger>& elements)
{
    if (elements.empty())
    {
        return;
    }

    // T08 / R-06: the modal "Edit Item" dialog is no longer on the main path.
    // The request only selects the element; the persistent Format panel then
    // shows its values in the inline modeless QTextEdit and writes them back
    // through onInlineTextChanged(). The panel is notified through
    // PDFPageContentScene::selectionChanged, so no direct refresh is requested
    // here - the panel coalesces the update into a single event loop turn.
    m_scene.setSelectedElementIds(elements);
}

void EditorPlugin::onSceneEditSingleElement(pdf::PDFInteger elementId)
{
    onSceneEditElement({ elementId });
}

void EditorPlugin::onPenChanged(const QPen& pen)
{
    const std::set<pdf::PDFInteger> selectedIds = m_scene.getSelectedElementIds();
    if (!selectedIds.empty())
    {
        std::vector<pdf::PDFPageContentElement*> replacements;
        for (pdf::PDFInteger id : selectedIds)
        {
            if (pdf::PDFPageContentElement* element = m_scene.getElementById(id))
            {
                std::unique_ptr<pdf::PDFPageContentElement> clonedElement(element->clone());
                if (pdf::PDFPageContentStyledElement* styled =
                        dynamic_cast<pdf::PDFPageContentStyledElement*>(clonedElement.get()))
                {
                    styled->setPen(pen);
                    replacements.push_back(clonedElement.release());
                }
            }
        }
        if (!replacements.empty())
        {
            m_scene.replaceElements(std::move(replacements));
            updateGraphics();
        }
        // Never fall back to the active tool setter: tool defaults must not be
        // polluted by the properties of a selected element (AC-13 / SAC-06).
        return;
    }

    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setPen(pen);
    }
}

void EditorPlugin::onBrushChanged(const QBrush& brush)
{
    const std::set<pdf::PDFInteger> selectedIds = m_scene.getSelectedElementIds();
    if (!selectedIds.empty())
    {
        std::vector<pdf::PDFPageContentElement*> replacements;
        for (pdf::PDFInteger id : selectedIds)
        {
            if (pdf::PDFPageContentElement* element = m_scene.getElementById(id))
            {
                std::unique_ptr<pdf::PDFPageContentElement> clonedElement(element->clone());
                if (pdf::PDFPageContentStyledElement* styled =
                        dynamic_cast<pdf::PDFPageContentStyledElement*>(clonedElement.get()))
                {
                    styled->setBrush(brush);
                    replacements.push_back(clonedElement.release());
                }
            }
        }
        if (!replacements.empty())
        {
            m_scene.replaceElements(std::move(replacements));
            updateGraphics();
        }
        return;
    }

    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setBrush(brush);
    }
}

void EditorPlugin::onFontChanged(const QFont& font)
{
    const std::set<pdf::PDFInteger> selectedIds = m_scene.getSelectedElementIds();
    if (!selectedIds.empty())
    {
        std::vector<pdf::PDFPageContentElement*> replacements;
        for (pdf::PDFInteger id : selectedIds)
        {
            if (pdf::PDFPageContentElement* element = m_scene.getElementById(id))
            {
                std::unique_ptr<pdf::PDFPageContentElement> clonedElement(element->clone());
                if (pdf::PDFPageContentElementTextBox* textBox =
                        dynamic_cast<pdf::PDFPageContentElementTextBox*>(clonedElement.get()))
                {
                    textBox->setFont(font);
                    replacements.push_back(clonedElement.release());
                }
            }
        }
        if (!replacements.empty())
        {
            m_scene.replaceElements(std::move(replacements));
            updateGraphics();
        }
        return;
    }

    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setFont(font);
    }
}

void EditorPlugin::onAlignmentChanged(Qt::Alignment alignment)
{
    const std::set<pdf::PDFInteger> selectedIds = m_scene.getSelectedElementIds();
    if (!selectedIds.empty())
    {
        std::vector<pdf::PDFPageContentElement*> replacements;
        for (pdf::PDFInteger id : selectedIds)
        {
            if (pdf::PDFPageContentElement* element = m_scene.getElementById(id))
            {
                std::unique_ptr<pdf::PDFPageContentElement> clonedElement(element->clone());
                if (pdf::PDFPageContentElementTextBox* textBox =
                        dynamic_cast<pdf::PDFPageContentElementTextBox*>(clonedElement.get()))
                {
                    textBox->setAlignment(alignment);
                    replacements.push_back(clonedElement.release());
                }
            }
        }
        if (!replacements.empty())
        {
            m_scene.replaceElements(std::move(replacements));
            updateGraphics();
        }
        return;
    }

    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setAlignment(alignment);
    }
}

void EditorPlugin::onTextAngleChanged(pdf::PDFReal angle)
{
    const std::set<pdf::PDFInteger> selectedIds = m_scene.getSelectedElementIds();
    if (!selectedIds.empty())
    {
        std::vector<pdf::PDFPageContentElement*> replacements;
        for (pdf::PDFInteger id : selectedIds)
        {
            if (pdf::PDFPageContentElement* element = m_scene.getElementById(id))
            {
                std::unique_ptr<pdf::PDFPageContentElement> clonedElement(element->clone());
                if (pdf::PDFPageContentElementTextBox* textBox =
                        dynamic_cast<pdf::PDFPageContentElementTextBox*>(clonedElement.get()))
                {
                    textBox->setAngle(angle);
                    replacements.push_back(clonedElement.release());
                }
            }
        }
        if (!replacements.empty())
        {
            m_scene.replaceElements(std::move(replacements));
            updateGraphics();
        }
        return;
    }

    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setTextAngle(angle);
    }
}

void EditorPlugin::setActive(bool active)
{
    if (m_scene.isActive() != active)
    {
        // Abort active tool, if we are deactivating the plugin
        if (!active)
        {
            // Unregister the persistent panel before m_scene is cleared, so it
            // never keeps a dangling scene pointer. Also close a possibly open
            // continuous edit transaction.
            unregisterFormatPanel();

            if (pdf::PDFWidgetTool* tool = m_widget->getToolManager()->getActiveTool())
            {
                auto it = std::find(m_tools.cbegin(), m_tools.cend(), tool);
                if (it != m_tools.cend())
                {
                    m_widget->getToolManager()->setActiveTool(nullptr);
                }
            }
        }

        m_scene.setActive(active);
        if (!active)
        {
            clearUndoRedo();
            m_scene.clear();
            m_editedPageContent.clear();
        }
        else
        {
            clearUndoRedo();
            updateDockWidget();
            registerFormatPanel();
            pdf::PDFTemporaryValueChange guard(&m_isUndoRedoInProgress, true);
            updateEditedPages();
            initializeUndoRedo();
        }

        m_actions[Activate]->setChecked(active);
        updateActions();

        // If editor is not active, remove the widget
        if (m_editorWidget && !active)
        {
            delete m_editorWidget;
            m_editorWidget = nullptr;
        }
    }
}

void EditorPlugin::onSetActive(bool active)
{
    if (m_scene.isActive() && !active && !save())
    {
        updateActions();
        m_actions[Activate]->setChecked(true);
        return;
    }

    setActive(active);
}

void EditorPlugin::onCreateMultipleElementsTriggered(bool enabled)
{
    for (pdf::PDFWidgetTool* tool : m_tools)
    {
        if (pdf::PDFCreatePCElementTool* createTool = qobject_cast<pdf::PDFCreatePCElementTool*>(tool))
        {
            createTool->setMultipleElementCreationEnabled(enabled);
        }
    }
}

void EditorPlugin::readSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());

    settings.beginGroup("EditorPlugin");
    const bool createMultipleElements = settings.value("CreateMultipleElements", false).toBool();
    settings.endGroup();

    m_actions[CreateMultipleElements]->setChecked(createMultipleElements);
    onCreateMultipleElementsTriggered(createMultipleElements);
}

void EditorPlugin::writeSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());

    settings.beginGroup("EditorPlugin");
    settings.setValue("CreateMultipleElements", m_actions[CreateMultipleElements]->isChecked());
    settings.endGroup();
}

void EditorPlugin::updateActions()
{
    m_actions[Activate]->setEnabled(m_document);

    if (!m_scene.isActive() || !m_document)
    {
        // Inactive scene - disable all except activate action and the settings
        // of the creation tools, which can be changed at any time
        for (QAction* action : m_actions)
        {
            if (action == m_actions[Activate])
            {
                action->setEnabled(m_widget && !m_widget->isAnySceneActive(&m_scene));
                continue;
            }

            if (action == m_actions[CreateMultipleElements])
            {
                continue;
            }

            action->setEnabled(false);
        }

        return;
    }

    const bool isSceneNonempty = !m_scene.isEmpty();
    const bool canUndo = !m_isSaving && !m_isUndoRedoInProgress && m_undoStates.size() > 1;
    const bool canRedo = !m_isSaving && !m_isUndoRedoInProgress && !m_redoStates.empty();

    // Tool actions
    for (auto actionId : { Text, FreehandCurve, AcceptMark, RejectMark,
                           Rectangle, RoundedRectangle, HorizontalLine,
                           VerticalLine, Line, Dot, SvgImage })
    {
        m_actions[actionId]->setEnabled(true);
    }

    m_actions[Undo]->setEnabled(canUndo);
    m_actions[Redo]->setEnabled(canRedo);

    // Clear action
    QAction* clearAction = m_actions[Clear];
    clearAction->setEnabled(isSceneNonempty);
}

void EditorPlugin::clearUndoRedo()
{
    m_undoStates.clear();
    m_redoStates.clear();
}

void EditorPlugin::initializeUndoRedo()
{
    clearUndoRedo();
    m_undoStates.emplace_back(m_scene.captureState());
}

void EditorPlugin::createUndoStep()
{
    if (m_isContinuousEditInProgress)
    {
        return;
    }

    m_undoStates.emplace_back(m_scene.captureState());
    m_redoStates.clear();

    while (m_undoStates.size() > MAX_UNDO_STEPS)
    {
        m_undoStates.erase(m_undoStates.begin());
    }
}

pdf::PDFPageContentScene::SceneState EditorPlugin::copySceneState(const pdf::PDFPageContentScene::SceneState& state) const
{
    pdf::PDFPageContentScene::SceneState copy;
    copy.selectedElementIds = state.selectedElementIds;
    copy.elements.reserve(state.elements.size());

    for (const auto& element : state.elements)
    {
        copy.elements.emplace_back(element->clone());
    }

    return copy;
}

void EditorPlugin::onUndoTriggered()
{
    if (m_undoStates.size() < 2)
    {
        return;
    }

    {
        pdf::PDFTemporaryValueChange guard(&m_isUndoRedoInProgress, true);
        m_redoStates.emplace_back(std::move(m_undoStates.back()));
        m_undoStates.pop_back();
        while (m_redoStates.size() > MAX_REDO_STEPS)
        {
            m_redoStates.erase(m_redoStates.begin());
        }

        m_scene.restoreState(copySceneState(m_undoStates.back()));
    }

    updateActions();
}

void EditorPlugin::onRedoTriggered()
{
    if (m_redoStates.empty())
    {
        return;
    }

    {
        pdf::PDFTemporaryValueChange guard(&m_isUndoRedoInProgress, true);
        pdf::PDFPageContentScene::SceneState state = std::move(m_redoStates.back());
        m_redoStates.pop_back();
        m_scene.restoreState(copySceneState(state));
        m_undoStates.emplace_back(std::move(state));
        while (m_undoStates.size() > MAX_UNDO_STEPS)
        {
            m_undoStates.erase(m_undoStates.begin());
        }
    }

    updateActions();
}

void EditorPlugin::updateGraphics()
{
    if (m_widget)
    {
        m_widget->getDrawWidget()->getWidget()->update();
    }
}

void EditorPlugin::updateDockWidget()
{
    if (m_editorWidget)
    {
        return;
    }

    m_editorWidget = new pdf::PDFPageContentEditorWidget(m_dataExchangeInterface->getMainWindow());
    m_editorWidget->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_dataExchangeInterface->getMainWindow()->addDockWidget(Qt::RightDockWidgetArea, m_editorWidget, Qt::Vertical);
    m_editorWidget->setFloating(false);
    m_editorWidget->setWindowTitle(tr("Editor Toolbox"));
    m_editorWidget->setScene(&m_scene);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::operationTriggered, &m_scene, &pdf::PDFPageContentScene::performOperation);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::itemSelectionChangedByUser, this, &EditorPlugin::onWidgetSelectionChanged);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::editElementRequest, this, &EditorPlugin::onSceneEditSingleElement);

    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::AlignTop))->setIcon(QIcon(":/resources/pce-align-top.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::AlignCenterVertically))->setIcon(QIcon(":/resources/pce-align-v-center.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::AlignBottom))->setIcon(QIcon(":/resources/pce-align-bottom.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::AlignLeft))->setIcon(QIcon(":/resources/pce-align-left.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::AlignCenterHorizontally))->setIcon(QIcon(":/resources/pce-align-h-center.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::AlignRight))->setIcon(QIcon(":/resources/pce-align-right.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::SetSameHeight))->setIcon(QIcon(":/resources/pce-same-height.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::SetSameWidth))->setIcon(QIcon(":/resources/pce-same-width.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::SetSameSize))->setIcon(QIcon(":/resources/pce-same-size.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::CenterHorizontally))->setIcon(QIcon(":/resources/pce-center-h.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::CenterVertically))->setIcon(QIcon(":/resources/pce-center-v.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::CenterHorAndVert))->setIcon(QIcon(":/resources/pce-center-vh.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::LayoutVertically))->setIcon(QIcon(":/resources/pce-layout-v.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::LayoutHorizontally))->setIcon(QIcon(":/resources/pce-layout-h.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::LayoutForm))->setIcon(QIcon(":/resources/pce-layout-form.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::LayoutGrid))->setIcon(QIcon(":/resources/pce-layout-grid.svg"));

    for (QAction* action : m_actions)
    {
        m_editorWidget->addAction(action);
    }

    // T05: the persistent Format panel is the single source of property
    // signals. Hide the old Appearance section, so the two docks cannot fight
    // over the same values. If the group box cannot be found, fall back to
    // disabling the five style controls.
    if (QWidget* appearanceGroup = m_editorWidget->findChild<QWidget*>("appearanceGroupBox"))
    {
        appearanceGroup->setVisible(false);
    }
    else
    {
        for (pdf::PDFPageContentEditorStyleSettings* settings :
             m_editorWidget->findChildren<pdf::PDFPageContentEditorStyleSettings*>())
        {
            settings->setEnabled(false);
        }
    }

    if (QMainWindow* mainWindow = m_dataExchangeInterface->getMainWindow())
    {
        if (pdf::PDFContextualFormatPanel* panel =
                mainWindow->findChild<pdf::PDFContextualFormatPanel*>())
        {
            mainWindow->splitDockWidget(panel, m_editorWidget, Qt::Vertical);
        }
    }
}

void EditorPlugin::registerFormatPanel()
{
    QMainWindow* mainWindow = m_dataExchangeInterface->getMainWindow();
    if (!mainWindow)
    {
        return;
    }

    pdf::PDFContextualFormatPanel* panel = mainWindow->findChild<pdf::PDFContextualFormatPanel*>();
    if (!panel)
    {
        return;
    }

    m_formatPanel = panel;
    panel->registerPageContentScene(&m_scene);

    connect(panel, &pdf::PDFContextualFormatPanel::penChanged,
            this, &EditorPlugin::onPenChanged, Qt::UniqueConnection);
    connect(panel, &pdf::PDFContextualFormatPanel::brushChanged,
            this, &EditorPlugin::onBrushChanged, Qt::UniqueConnection);
    connect(panel, &pdf::PDFContextualFormatPanel::fontChanged,
            this, &EditorPlugin::onFontChanged, Qt::UniqueConnection);
    connect(panel, &pdf::PDFContextualFormatPanel::alignmentChanged,
            this, &EditorPlugin::onAlignmentChanged, Qt::UniqueConnection);
    connect(panel, &pdf::PDFContextualFormatPanel::textAngleChanged,
            this, &EditorPlugin::onTextAngleChanged, Qt::UniqueConnection);
    connect(panel, &pdf::PDFContextualFormatPanel::inlineTextChanged,
            this, &EditorPlugin::onInlineTextChanged, Qt::UniqueConnection);
    connect(panel, &pdf::PDFContextualFormatPanel::continuousEditStarted,
            this, &EditorPlugin::onContinuousEditStarted, Qt::UniqueConnection);
    connect(panel, &pdf::PDFContextualFormatPanel::continuousEditFinished,
            this, &EditorPlugin::onContinuousEditFinished, Qt::UniqueConnection);
    connect(panel, &pdf::PDFContextualFormatPanel::setAsDefaultRequested,
            this, &EditorPlugin::onSetAsDefaultRequested, Qt::UniqueConnection);
    connect(panel, &pdf::PDFContextualFormatPanel::annotationDefaultStyleRequested,
            this, &EditorPlugin::onAnnotationDefaultStyleRequested, Qt::UniqueConnection);
}

void EditorPlugin::onSetAsDefaultRequested(const QFont& font, const QColor& color, Qt::Alignment alignment)
{
    pdf::PDFWidgetTool* activeTool = getActiveTool();
    if (!activeTool)
    {
        return;
    }

    // A free text annotation stores its style in PDFFreeTextStyle, so it is
    // promoted through the tool itself instead of PDFCreatePCElementTool.
    if (pdf::PDFCreateFreeTextTool* freeTextTool = qobject_cast<pdf::PDFCreateFreeTextTool*>(activeTool))
    {
        freeTextTool->updateStyleFromPanel(font, alignment, color);
        return;
    }

    pdf::PDFCreatePCElementTool* elementTool = qobject_cast<pdf::PDFCreatePCElementTool*>(activeTool);
    if (!elementTool)
    {
        return;
    }

    // Only the defaults of the tool are written. getElement() returns the
    // element the tool keeps for the next creation, not the current selection,
    // so no selected object is modified here (AC-14).
    if (pdf::PDFPageContentElement* element = elementTool->getElement())
    {
        if (pdf::PDFPageContentStyledElement* styled = dynamic_cast<pdf::PDFPageContentStyledElement*>(element))
        {
            // The pen width of the tool is kept, only the colour is promoted.
            QPen pen = styled->getPen();
            pen.setColor(color);
            elementTool->setPen(pen);
        }
    }

    elementTool->setFont(font);
    elementTool->setAlignment(alignment);
}

void EditorPlugin::onAnnotationDefaultStyleRequested(const QColor& color)
{
    if (!color.isValid())
    {
        return;
    }

    pdf::PDFWidgetTool* activeTool = getActiveTool();
    if (!activeTool)
    {
        return;
    }

    pdf::PDFAnnotationStyleManager* styleManager = activeTool->getAnnotationStyleManager();
    if (!styleManager)
    {
        return;
    }

    // PDFAnnotationStyle has strokeColor / fillColor / penWidth only and no
    // opacity member, so the opacity slider of the panel is intentionally
    // ignored here (known limitation KI-06 / G-14).
    pdf::PDFAnnotationStyle style = styleManager->getStyle();
    style.strokeColor = color;
    style.fillColor = color;
    styleManager->setStyle(style);
}

void EditorPlugin::unregisterFormatPanel()
{
    if (m_isContinuousEditInProgress)
    {
        onContinuousEditFinished();
    }

    if (!m_formatPanel)
    {
        return;
    }

    m_formatPanel->unregisterPageContentScene(&m_scene);
    QObject::disconnect(m_formatPanel.data(), nullptr, this, nullptr);
    m_formatPanel = nullptr;
}

void EditorPlugin::onContinuousEditStarted()
{
    if (m_isContinuousEditInProgress)
    {
        return;
    }

    // Undo steps created during the interaction are suppressed, exactly one
    // step is pushed when the interaction finishes.
    m_isContinuousEditInProgress = true;
}

void EditorPlugin::onContinuousEditFinished()
{
    if (!m_isContinuousEditInProgress)
    {
        return;
    }

    m_isContinuousEditInProgress = false;

    // The state *after* the interaction is pushed. The state before it is
    // already on the stack (the last regular step), so pushing the pending
    // pre-interaction snapshot would make the redo of the step a no-op.
    m_undoStates.emplace_back(m_scene.captureState());
    m_redoStates.clear();
    while (m_undoStates.size() > MAX_UNDO_STEPS)
    {
        m_undoStates.erase(m_undoStates.begin());
    }
    updateActions();
}

void EditorPlugin::onInlineTextChanged(const QString& text)
{
    const std::set<pdf::PDFInteger> selectedIds = m_scene.getSelectedElementIds();
    if (selectedIds.empty())
    {
        return;
    }

    // R-7: every selected text box receives the new text, not only the first
    // one. The clones are collected and committed in a single batch, so the
    // scene emits one change notification and the undo history gets one step.
    std::vector<pdf::PDFPageContentElement*> replacements;

    for (pdf::PDFInteger id : selectedIds)
    {
        pdf::PDFPageContentElement* element = m_scene.getElementById(id);
        if (!element)
        {
            continue;
        }

        std::unique_ptr<pdf::PDFPageContentElement> clonedElement(element->clone());

        if (pdf::PDFPageContentElementTextBox* textBox =
                dynamic_cast<pdf::PDFPageContentElementTextBox*>(clonedElement.get()))
        {
            textBox->setText(text);
            replacements.push_back(clonedElement.release());
            continue;
        }

        // R-4: the text of the original page content is a part of the content
        // stream, so the element has to be regenerated by the content editor -
        // this is the same code path the removed "Edit Item" dialog used. The
        // element must not be silently ignored.
        pdf::PDFPageContentElementEdited* editedElement =
            dynamic_cast<pdf::PDFPageContentElementEdited*>(clonedElement.get());
        if (editedElement && editedElement->getElement() &&
            editedElement->getElement()->asText())
        {
            if (updateTextElement(editedElement))
            {
                replacements.push_back(clonedElement.release());
            }
        }
    }

    if (!replacements.empty())
    {
        m_scene.replaceElements(std::move(replacements));
        updateGraphics();
    }
}

void EditorPlugin::updateEditedPages()
{
    if (!m_scene.isActive() || m_isSaving)
    {
        // Editor is not active or we are saving the document
        return;
    }

    std::vector<pdf::PDFInteger> currentPages = m_widget->getDrawWidget()->getCurrentPages();
    for (pdf::PDFInteger pageIndex : currentPages)
    {
        if (m_editedPageContent.count(pageIndex))
        {
            continue;
        }

        const pdf::PDFPage* page = m_document->getCatalog()->getPage(pageIndex);
        auto cms = m_widget->getDrawWidgetProxy()->getCMSManager()->getCurrentCMS();
        pdf::PDFPageContentEditorProcessor processor(page,
                                                     m_document,
                                                     m_widget->getDrawWidgetProxy()->getFontCache(),
                                                     m_widget->getDrawWidgetProxy()->getIccProfileCache(),
                                                     cms.data(),
                                                     m_widget->getDrawWidgetProxy()->getOptionalContentActivity(),
                                                     QTransform(),
                                                     pdf::PDFMeshQualitySettings());

        QList<pdf::PDFRenderError> errors = processor.processContents();
        Q_UNUSED(errors);

        m_editedPageContent[pageIndex] = processor.takeEditedPageContent();

        size_t elementCount = m_editedPageContent[pageIndex].getElementCount();
        for (size_t i = 0; i < elementCount; ++i)
        {
            pdf::PDFEditedPageContentElement* element = m_editedPageContent[pageIndex].getElement(i);
            pdf::PDFPageContentElementEdited* editedElement = new pdf::PDFPageContentElementEdited(element);
            editedElement->setPageIndex(pageIndex);
            m_scene.addElement(editedElement);
        }
    }
}

bool EditorPlugin::updateTextElement(pdf::PDFPageContentElementEdited* element)
{
    pdf::PDFPageContentElementEdited* elementEdited = dynamic_cast<pdf::PDFPageContentElementEdited*>(element);
    if (!elementEdited)
    {
        return false;
    }

    pdf::PDFEditedPageContentElementText* targetTextElement = elementEdited->getElement()->asText();

    if (!targetTextElement)
    {
        return false;
    }

    pdf::PDFDocumentModifier modifier(m_document);
    pdf::PDFDocumentBuilder* builder = modifier.getBuilder();

    if (!updatePageContent(element->getPageIndex(), { element }, builder))
    {
        return false;
    }

    if (modifier.finalize())
    {
        pdf::PDFDocument* document = modifier.getDocument().get();

        const pdf::PDFPage* page = document->getCatalog()->getPage(element->getPageIndex());
        auto cms = m_widget->getDrawWidgetProxy()->getCMSManager()->getCurrentCMS();
        pdf::PDFFontCache fontCache(64, 64);
        pdf::PDFICCProfileCache iccProfileCache;
        pdf::PDFOptionalContentActivity activity(document, pdf::OCUsage::View, nullptr);
        fontCache.setDocument(pdf::PDFModifiedDocument(document, &activity));
        iccProfileCache.setDocument(pdf::PDFModifiedDocument(document, &activity));
        pdf::PDFPageContentEditorProcessor processor(page, document, &fontCache, &iccProfileCache, cms.data(), &activity, QTransform(), pdf::PDFMeshQualitySettings());

        QList<pdf::PDFRenderError> errors = processor.processContents();
        Q_UNUSED(errors);

        pdf::PDFEditedPageContent content = processor.takeEditedPageContent();
        if (content.getElementCount() == 1)
        {
            pdf::PDFEditedPageContentElement* sourceElement = content.getElement(0);
            pdf::PDFEditedPageContentElementText* sourceElementText = sourceElement->asText();
            if (!sourceElementText)
            {
                return false;
            }
            targetTextElement->setState(sourceElementText->getState());
            targetTextElement->setTextPath(sourceElementText->getTextPath());
            targetTextElement->setItems(sourceElementText->getItems());
            targetTextElement->setTransform(sourceElementText->getTransform());
            targetTextElement->setClipPath(sourceElementText->getClipPath());

            // The font keys of the processed page can differ from the keys of the target
            // element (fonts are written under the keys of the page resources), so the
            // font resources and the text items as text must correspond to each other.
            // The processed document is temporary - font objects created only in it
            // (for example a fallback font) are not valid in the edited document.
            targetTextElement->setFontResources(pdf::PDFEditedPageContentElementText::getFontResourcesValidInDocument(sourceElementText->getFontResources(), document, m_document));
            targetTextElement->setItemsAsText(sourceElementText->getItemsAsText());
        }
        else
        {
            return false;
        }
    }
    else
    {
        // finalize() failed, the document was not modified. Reporting success
        // here would make the caller replace the element with a stale clone.
        return false;
    }

    return true;
}

void EditorPlugin::onDrawSpaceChanged()
{
    const size_t elementCount = m_scene.getElementIds().size();
    pdf::PDFTemporaryValueChange guard(&m_isUndoRedoInProgress, true);
    updateEditedPages();

    if (m_scene.getElementIds().size() != elementCount)
    {
        initializeUndoRedo();
        updateActions();
    }
}

}

/**
 * Copyright (C) 2011  Charlie Sharpsteen, Stefan Löffler
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#include "PDFDocumentView.h"
#ifdef DEBUG
#include <QDebug>
#endif

// Some utility functions.
//
// **TODO:** _Find a better place to put these._
static bool isPageItem(QGraphicsItem *item) { return ( item->type() == PDFPageGraphicsItem::Type ); }

// PDFDocumentView
// ===============

// This class descends from `QGraphicsView` and is responsible for controlling
// and displaying the contents of a `Poppler::Document` using a `QGraphicsScene`.
PDFDocumentView::PDFDocumentView(QWidget *parent):
  Super(parent),
  _zoomLevel(1.0),
  _pageMode(PageMode_OneColumnContinuous),
  _mouseMode(MouseMode_MagnifyingGlass)
{
  setBackgroundRole(QPalette::Dark);
  setAlignment(Qt::AlignCenter);
  setFocusPolicy(Qt::StrongFocus);

  // If _currentPage is not set to -1, the compiler may default to 0. In that
  // case, `goFirst()` or `goToPage(0)` will fail because the view will think
  // it is already looking at page 0.
  _currentPage = -1;
  _magnifier = new PDFDocumentMagnifierView(this);
}


// Accessors
// ---------
void PDFDocumentView::setScene(PDFDocumentScene *a_scene)
{
  Super::setScene(a_scene);

  // **TODO:** _Replace with an overloaded `scene` method._
  _pdf_scene = a_scene;

  _lastPage = a_scene->lastPage();

  // Respond to page jumps requested by the `PDFDocumentScene`.
  //
  // **TODO:**
  // _May want to consider not doing this by default. It is conceivable to have
  // a View that would ignore page jumps that other scenes would respond to._
  connect(a_scene, SIGNAL(pageChangeRequested(int)), this, SLOT(goToPage(int)));
  connect(this, SIGNAL(changedPage(int)), this, SLOT(maybeUpdateSceneRect()));
}
int PDFDocumentView::currentPage() { return _currentPage; }
int PDFDocumentView::lastPage()    { return _lastPage; }

void PDFDocumentView::setPageMode(PageMode pageMode)
{
  if (!_pdf_scene || pageMode == _pageMode)
    return;

  // Save the current view relative to the current page so we can restore it
  // after changing the mode
  // **TODO:** Safeguard
  QRectF viewRect(mapToScene(viewport()->rect()).boundingRect());
  viewRect.translate(-_pdf_scene->pageAt(_currentPage)->pos());

  // **TODO:** Avoid relayouting everything twice when switching from SinglePage
  // to TwoColumnContinuous (once by setContinuous(), and a second time by
  // setColumnCount() below)
  switch (pageMode) {
    case PageMode_SinglePage:
      _pdf_scene->showOnePage(_currentPage);
      _pdf_scene->pageLayout().setContinuous(false);
      break;
    case PageMode_OneColumnContinuous:
      if (_pageMode == PageMode_SinglePage) {
        _pdf_scene->pageLayout().setContinuous(true);
        _pdf_scene->showAllPages();
        // Reset the scene rect; causes it the encompass the whole scene
        setSceneRect(QRectF());
      }
      _pdf_scene->pageLayout().setColumnCount(1, 0);
      break;
    case PageMode_TwoColumnContinuous:
      if (_pageMode == PageMode_SinglePage) {
        _pdf_scene->pageLayout().setContinuous(true);
        _pdf_scene->showAllPages();
        // Reset the scene rect; causes it the encompass the whole scene
        setSceneRect(QRectF());
      }
      _pdf_scene->pageLayout().setColumnCount(2, 1);
      break;
  }
  _pageMode = pageMode;
  _pdf_scene->pageLayout().relayout();

  // We might need to update the scene rect (when switching to single page mode)
  maybeUpdateSceneRect();

  // Restore the view from before as good as possible
  viewRect.translate(_pdf_scene->pageAt(_currentPage)->pos());
  ensureVisible(viewRect, 0, 0);
}


// Public Slots
// ------------
// **TODO:** goPrev() and goNext() should not (necessarily) center on top of page
void PDFDocumentView::goPrev()  { goToPage(_currentPage - 1); }
void PDFDocumentView::goNext()  { goToPage(_currentPage + 1); }
void PDFDocumentView::goFirst() { goToPage(0); }
void PDFDocumentView::goLast()  { goToPage(_lastPage - 1); }

// **TODO:** _Overload this function to take `PDFPageGraphicsItem` as a
// parameter?_
void PDFDocumentView::goToPage(int pageNum)
{
  // We silently ignore any invalid page numbers.
  if ( (pageNum >= 0) && (pageNum < _lastPage) && (pageNum != _currentPage) )
  {
    if (!_pdf_scene || _pdf_scene->pages().size() <= pageNum || !_pdf_scene->pageAt(pageNum))
      return;
    moveTopLeftTo(_pdf_scene->pageAt(pageNum)->pos());

    _currentPage = pageNum;
    if (_pageMode == PageMode_SinglePage && _pdf_scene)
      _pdf_scene->showOnePage(_currentPage);
    emit changedPage(_currentPage);
  }
}


void PDFDocumentView::zoomIn()
{
  _zoomLevel *= 3.0/2.0;
  // Set the transformation anchor to AnchorViewCenter so we always zoom into
  // the center of the view (rather than into the upper left corner)
  QGraphicsView::ViewportAnchor anchor = transformationAnchor();
  setTransformationAnchor(QGraphicsView::AnchorViewCenter);
  this->scale(3.0/2.0, 3.0/2.0);
  setTransformationAnchor(anchor);
  emit changedZoom(_zoomLevel);
}

void PDFDocumentView::zoomOut()
{
  _zoomLevel *= 2.0/3.0;
  // Set the transformation anchor to AnchorViewCenter so we always zoom out of
  // the center of the view (rather than out of the upper left corner)
  QGraphicsView::ViewportAnchor anchor = transformationAnchor();
  setTransformationAnchor(QGraphicsView::AnchorViewCenter);
  this->scale(2.0/3.0, 2.0/3.0);
  setTransformationAnchor(anchor);
  emit changedZoom(_zoomLevel);
}

void PDFDocumentView::setMouseMode(const MouseMode newMode)
{
  if (_mouseMode == newMode)
    return;

  if (_mouseMode == MouseMode_Move)
    setDragMode(QGraphicsView::ScrollHandDrag);

  switch (newMode) {
    case MouseMode_Move:
      setDragMode(QGraphicsView::ScrollHandDrag);
      break;
  }
  _mouseMode = newMode;
}

void PDFDocumentView::setMagnifierShape(const MagnifierShape shape)
{
  if (_magnifier)
    _magnifier->setShape(shape);
}

void PDFDocumentView::setMagnifierSize(const int size)
{
  if (_magnifier)
    _magnifier->setSize(size);
}

// Protected Slots
// --------------
void PDFDocumentView::maybeUpdateSceneRect() {
  if (!_pdf_scene || _pageMode != PageMode_SinglePage)
    return;

  // Set the scene rect of the view, i.e., the rect accessible via the scroll
  // bars. In single page mode, this must be the rect of the current page
  // **TODO:** Safeguard
  setSceneRect(_pdf_scene->pageAt(_currentPage)->sceneBoundingRect());
}

// Event Handlers
// --------------

// Keep track of the current page by overloading the widget paint event.
void PDFDocumentView::paintEvent(QPaintEvent *event)
{
  Super::paintEvent(event);

  // After `QGraphicsView` has taken care of updates to this widget, find the
  // currently displayed page. We do this by grabbing all items that are
  // currently within the bounds of the viewport's top half. We take the
  // first item found to be the "current page".
  //
  // **NOTE:**
  // _If graphics objects other than `PDFPageGraphicsItem` are ever added to
  // the `GraphicsScene` managed by `PDFDocumentView` (such as annotations,
  // form elements, etc), it may be wise to ensure this selection only
  // considers `PDFPagegraphicsItem` objects._
  //
  // _A way to do this may be to call `toSet` on both `pages` and the result
  // of `items` and then take the first item of a set intersection._
  QRect pageBbox = viewport()->rect();
  pageBbox.setHeight(0.5 * pageBbox.height());
  int nextCurrentPage = _pdf_scene->pageNumAt(mapToScene(pageBbox));

  if ( nextCurrentPage != _currentPage && nextCurrentPage >= 0 && nextCurrentPage < _lastPage )
  {
    _currentPage = nextCurrentPage;
    emit changedPage(_currentPage);
  }
}

// **TODO:**
//
//   * _Should we let some parent widget worry about delegating Page
//     Up/PageDown/other keypresses?_
// **TODO:** Handle mouseWheel/Key_Up/Key_Down events at the edge of pages in
// single page mode
void PDFDocumentView::keyPressEvent(QKeyEvent *event)
{
  switch ( event->key() )
  {

    case Qt::Key_PageUp:
      goPrev();
      event->accept();
      break;

    case Qt::Key_PageDown:
      goNext();
      event->accept();
      break;

    case Qt::Key_Home:
      goFirst();
      event->accept();
      break;

    case Qt::Key_End:
      goLast();
      event->accept();
      break;

    default:
      Super::keyPressEvent(event);
      break;

  }
}

void PDFDocumentView::mousePressEvent(QMouseEvent * event)
{
  Super::mousePressEvent(event);

  // Don't do anything if the event was handled elsewhere (e.g., by a
  // PDFLinkGraphicsItem)
  if (event->isAccepted())
    return;

  switch (_mouseMode) {
    case MouseMode_MagnifyingGlass:
      // We only handle left mouse button events; no composites (like left+right
      // mouse buttons)
      if (_magnifier && event->buttons() == Qt::LeftButton) {
        _magnifier->prepareToShow();
        _magnifier->setPosition(event->pos());
        _magnifier->show();
      }
      break;
    default:
      // Nothing to do
      break;
  }
}

void PDFDocumentView::mouseMoveEvent(QMouseEvent * event)
{
  Super::mouseMoveEvent(event);

  // We don't check for event->isAccepted() here; for one, this always seems to
  // return true (for whatever reason), but more importantly, without enabling
  // mouse tracking we only receive this event if the current widget has grabbed
  // the mouse (i.e., after a mousePressEvent and before the corresponding
  // mouseReleaseEvent)

  switch (_mouseMode) {
    case MouseMode_MagnifyingGlass:
      if (_magnifier && _magnifier->isVisible())
        _magnifier->setPosition(event->pos());
      break;
    default:
      // Nothing to do
      break;
  }
}

void PDFDocumentView::mouseReleaseEvent(QMouseEvent * event)
{
  Super::mouseReleaseEvent(event);

  // We don't check for event->isAccepted() here; for one, this always seems to
  // return true (for whatever reason), but more importantly, without enabling
  // mouse tracking we only receive this event if the current widget has grabbed
  // the mouse (i.e., after a mousePressEvent)

  switch (_mouseMode) {
    case MouseMode_MagnifyingGlass:
      if (_magnifier && _magnifier->isVisible())
        _magnifier->hide();
      break;
    default:
      // Nothing to do
      break;
  }
}


// Other
// -----
void PDFDocumentView::moveTopLeftTo(const QPointF scenePos) {
  QRectF r(mapToScene(viewport()->rect()).boundingRect());
  r.moveTopLeft(scenePos);
  
  // **TODO:** Investigate why this approach doesn't work during startup if
  // the margin is set to 0
  ensureVisible(r, 1, 1);
}


// PDFDocumentMagnifierView
// ========================
//
PDFDocumentMagnifierView::PDFDocumentMagnifierView(PDFDocumentView *parent /* = 0 */) :
  Super(parent),
  _parent_view(parent),
  _zoomFactor(2.0),
  _zoomLevel(1.0),
  _shape(PDFDocumentView::Magnifier_Circle),
  _size(300)
{
  // the magnifier should initially be hidden
  hide();

  // suppress scrollbars
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  // suppress any border styling
  setFrameShape(QFrame::NoFrame);

  if (parent) {
    // transfer some settings from the parent view
    setBackgroundRole(parent->backgroundRole());
    setAlignment(parent->alignment());
  }

  setShape(_shape);
}

void PDFDocumentMagnifierView::prepareToShow()
{
  qreal zoomLevel;

  if (!_parent_view)
    return;

  // Ensure we have the same scene
  if (_parent_view->scene() != scene())
    setScene(_parent_view->scene());
  // Fix the zoom
  zoomLevel = _parent_view->zoomLevel() * _zoomFactor;
  if (zoomLevel != _zoomLevel)
    scale(zoomLevel / _zoomLevel, zoomLevel / _zoomLevel);
  _zoomLevel = zoomLevel;
  // Ensure we have enough padding at the border that we can display the
  // magnifier even beyond the edge
  setSceneRect(_parent_view->sceneRect().adjusted(-width() / _zoomLevel, -height() / _zoomLevel, width() / _zoomLevel, height() / _zoomLevel));
}

void PDFDocumentMagnifierView::setZoomFactor(const qreal zoomFactor)
{
  _zoomFactor = zoomFactor;
  // Actual handling of zoom levels happens in prepareToShow, as the zoom level
  // of the parent cannot change while the magnifier is shown
}

void PDFDocumentMagnifierView::setPosition(const QPoint pos)
{
  move(pos.x() - width() / 2, pos.y() - height() / 2);
  centerOn(_parent_view->mapToScene(pos));
}

void PDFDocumentMagnifierView::setShape(const PDFDocumentView::MagnifierShape shape)
{
  _shape = shape;

  // ensure the window rect is set properly for the new mode
  setSize(_size);

  switch (shape) {
    case PDFDocumentView::Magnifier_Rectangle:
      clearMask();
      break;
    case PDFDocumentView::Magnifier_Circle:
      setMask(QRegion(rect(), QRegion::Ellipse));
      break;
  }
}

void PDFDocumentMagnifierView::setSize(const int size)
{
  _size = size;
  switch (_shape) {
    case PDFDocumentView::Magnifier_Rectangle:
      setFixedSize(size * 4 / 3, size);
      break;
    case PDFDocumentView::Magnifier_Circle:
      setFixedSize(size, size);
      break;
  }
}


// PDFDocumentScene
// ================
//
// A large canvas that manages the layout of QGraphicsItem subclasses. The
// primary items we are concerned with are PDFPageGraphicsItem and
// PDFLinkGraphicsItem.
//
// The scene also holds a mutex which is used to serialize calls by child items
// (mostly PDFGraphicsPage) to the Poppler library as Poppler is not thread
// safe.
//
// This system may need to be re-worked because if another function somewhere
// accesses the Poppler document pointed to by `*a_doc` while the scene child
// items are executing tasks, we can produce a segfault. Because of this, the
// mutex may need to be held at a higher level.
PDFDocumentScene::PDFDocumentScene(Poppler::Document *a_doc, QObject *parent):
  Super(parent),
  _doc(a_doc),
  docMutex(new QMutex)
{
  // We need to register a QList<PDFLinkGraphicsItem *> meta-type so we can
  // pass it through inter-thread (i.e., queued) connections
  qRegisterMetaType< QList<PDFLinkGraphicsItem *> >();

  // **TODO:** _Investigate the Arthur backend for native Qt rendering._
  _doc->setRenderBackend(Poppler::Document::SplashBackend);
  // Make things look pretty.
  _doc->setRenderHint(Poppler::Document::Antialiasing);
  _doc->setRenderHint(Poppler::Document::TextAntialiasing);

  _lastPage = _doc->numPages();
  
  connect(&_pageLayout, SIGNAL(layoutChanged(const QRectF)), this, SLOT(pageLayoutChanged(const QRectF)));

  // Create a `PDFPageGraphicsItem` for each page in the PDF document and let
  // them be layed out by a `PDFPageLayout` instance.
  int i;
  PDFPageGraphicsItem *pagePtr;

  for (i = 0; i < _lastPage; ++i)
  {
    pagePtr = new PDFPageGraphicsItem(_doc->page(i));
    _pages.append(pagePtr);
    addItem(pagePtr);
    _pageLayout.addPage(pagePtr);
  }
  _pageLayout.relayout();
}


// Accessors
// ---------

QList<QGraphicsItem*> PDFDocumentScene::pages() { return _pages; };

// Overloaded method that returns all page objects inside a given rectangular
// area. First, `items` is used to grab all items inside the rectangle. This
// list is then filtered by item type so that it contains only references to
// `PDFPageGraphicsItem` objects.
QList<QGraphicsItem*> PDFDocumentScene::pages(const QPolygonF &polygon)
{
  QList<QGraphicsItem*> pageList = items(polygon);
  QtConcurrent::blockingFilter(pageList, isPageItem);

  return pageList;
};

// Convenience function to avoid moving the complete list of pages around
// between functions if only one page is needed
QGraphicsItem* PDFDocumentScene::pageAt(const int idx)
{
  if (idx < 0 || idx >= _pages.size())
    return NULL;
  return _pages[idx];
}

// This is a convenience function for returning the page number of the first
// page item inside a given area of the scene. If no page is in the specified
// area, -1 is returned.
int PDFDocumentScene::pageNumAt(const QPolygonF &polygon)
{
  QList<QGraphicsItem*> p(pages(polygon));
  if (p.isEmpty())
    return -1;
  return _pages.indexOf(p.first());
}

int PDFDocumentScene::pageNumFor(PDFPageGraphicsItem * const graphicsItem) const
{
  return _pages.indexOf(graphicsItem);
}

int PDFDocumentScene::lastPage() { return _lastPage; }

// Event Handlers
// --------------

// We re-implement the main event handler for the scene so that we can
// translate events generated by child items into signals that can be sent out
// to the rest of the program.
bool PDFDocumentScene::event(QEvent *event)
{
  if ( event->type() == PDFLinkEvent::LinkEvent )
  {
    event->accept();
    // Cast to a pointer for `PDFLinkEvent` so that we can access the `pageNum`
    // field.
    const PDFLinkEvent *link_event = dynamic_cast<const PDFLinkEvent*>(event);

    // Translate into a signal that can be handled by some other part of the
    // program, such as a `PDFDocumentView`.
    emit pageChangeRequested(link_event->pageNum);
    return true;
  }

  return Super::event(event);
}

// Protected Slots
// --------------
void PDFDocumentScene::pageLayoutChanged(const QRectF& sceneRect)
{
  setSceneRect(sceneRect);
  emit pageLayoutChanged();
}

// Other
// -----
void PDFDocumentScene::showOnePage(const int pageIdx) const
{
  int i;

  for (i = 0; i < _pages.size(); ++i) {
    if (!isPageItem(_pages[i]))
      continue;
    _pages[i]->setVisible(i == pageIdx);
  }
}

void PDFDocumentScene::showAllPages() const
{
  int i;

  for (i = 0; i < _pages.size(); ++i) {
    if (!isPageItem(_pages[i]))
      continue;
    _pages[i]->setVisible(true);
  }
}


// PDFPageGraphicsItem
// ===================

// This class descends from `QGraphicsObject` and implements the on-screen
// representation of `Poppler::Page` objects.
PDFPageGraphicsItem::PDFPageGraphicsItem(Poppler::Page *a_page, QGraphicsItem *parent):
  Super(parent),
  _page(a_page),
  _dpiX(QApplication::desktop()->physicalDpiX()),
  _dpiY(QApplication::desktop()->physicalDpiY()),

  _linksLoaded(false),
  _pageIsRendering(false),
  _zoomLevel(0.0),
  _magnifiedZoomLevel(0.0)
{
  // Create an empty pixmap that is the same size as the PDF page. This
  // allows us to delay the rendering of pages until they actually come into
  // view yet still know what the page size is.
  _pageSize = _page->pageSizeF();
  _pageSize.setWidth(_pageSize.width() * _dpiX / 72.0);
  _pageSize.setHeight(_pageSize.height() * _dpiY / 72.0);

  _pageScale = QTransform::fromScale(_pageSize.width(), _pageSize.height());
  // If we have a thumbnail image, use that as temporary image
  if (_page && !_page->thumbnail().isNull())
    _temporaryPage = QPixmap::fromImage(_page->thumbnail()).scaled(_pageSize.toSize());
  _renderedPage = QPixmap(_pageSize.toSize());
}

QRectF PDFPageGraphicsItem::boundingRect() const { return QRectF(QPointF(0.0, 0.0), _pageSize); }
int PDFPageGraphicsItem::type() const { return Type; }

// An overloaded paint method allows us to render the contents of
// `Poppler::Page` objects to `QImage` objects which are then stored inside the
// `PDFPageGraphicsItem` object using a `QPixmap`.
void PDFPageGraphicsItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
  // Really, there is an X scaling factor and a Y scaling factor, but we assume
  // that the X scaling factor is equal to the Y scaling factor.
  qreal scaleFactor = painter->transform().m11();
  PDFDocumentScene * docScene = qobject_cast<PDFDocumentScene*>(scene());

  // If this is the first time this `PDFPageGraphicsItem` has come into view,
  // `_linksLoaded` will be `false`. We then load all of the links on the page.
  if ( not _linksLoaded )
  {
    if (docScene) {
      // Connect the request object's signal to this object to receive a
      // notification when the result is available.
      // Note: We don't need to disconnect, as that is handled by Qt
      // automatically when the request object is destroyed later on
      // Note: addWorkItem must be separate, as otherwise the processing might
      // finish before we have actually finished initialization (i.e.,
      // finished connecting to the signal)
      PageProcessingLoadLinksRequest * request = docScene->processingThread().requestLoadLinks(this);
      if (request) {
        connect(request, SIGNAL(linksReady(QList<PDFLinkGraphicsItem *>)), this, SLOT(addLinks(QList<PDFLinkGraphicsItem *>)));
        docScene->processingThread().addPageProcessingRequest(request);
      }
    }

    _linksLoaded = true;

    // This is a hack to give a nice white fill to pages that have not been
    // rendered. Would be nice to replace this with a "loading page" graphic.
    _renderedPage.fill();
  }

  // The transformation matrix of the `painter` object contains information
  // such as the current zoom level of the widget viewing this PDF page. We use
  // this matrix to position the page and then reset the transformation matrix
  // to an identity matrix as the page image has already been resized during
  // rendering.
  QPointF origin = painter->transform().map(QPointF(0.0, 0.0));

  // **TODO:** This way of detecting whether this is a magnifier request
  // seems a bit hokey; we should probably use some sort of type() method
  // **TODO:** Find a better way than to repeat the same code twice (once for
  // the magnifier, and once for the normal display)
  if (widget && widget->parent() && widget->parent()->inherits("PDFDocumentMagnifierView")) {
    if ( (_magnifiedZoomLevel != scaleFactor) && not _pageIsRendering )
    {
      // Indicate that a render is in progress so that subsequent paint events
      // won't trigger a re-render. Once `_pageImageGenerator` emits a `finished`
      // signal, this boolean is cleared.
      _pageIsRendering = true;

      if (docScene) {
        // Connect the request object's signal to this object to receive a
        // notification when the finished page is available.
        // Note: We don't need to disconnect, as that is handled by Qt
        // automatically when the request object is destroyed later on
        // Note: addWorkItem must be separate, as otherwise the processing might
        // finish before we have actually finished initialization (i.e.,
        // finished connecting to the signal)
        PageProcessingRenderPageRequest * request = docScene->processingThread().requestRenderPage(this, scaleFactor);
        if (request) {
          connect(request, SIGNAL(pageImageReady(qreal, QImage)), this, SLOT(updateMagnifiedPage(qreal, QImage)));
          docScene->processingThread().addPageProcessingRequest(request);
        }
      }

      _magnifiedZoomLevel = scaleFactor;
    }
    if ( _pageIsRendering ) {
      // A new resized page is still rendering, so we "blow up" our current
      // render and paint that. For performance reasons, we store this so we don't
      // need to recreate it in every paint event.
      // Note: Creating the scaled pixmap can take some time at high
      // magnifications, as can copying the fully rendered page from the rendering
      // thread into _renderedPage. Both cause a small lag on the UI.
      // **TODO:** Investigate if it would help to only move pointers around. In
      // this case, we'd have to take care which thread an image belongs to before
      // assigning/deallocating.
      QSizeF scaledSize = painter->transform().mapRect(boundingRect()).size();

      if (_temporaryMagnifiedPage.isNull() || _temporaryMagnifiedPage.size() != scaledSize.toSize())
        _temporaryMagnifiedPage = _renderedPage.scaled(scaledSize.toSize());

      painter->setTransform(QTransform());
      painter->drawPixmap(origin, _temporaryMagnifiedPage);
    } else {
      painter->setTransform(QTransform());
      painter->drawPixmap(origin, _magnifiedPage);
    }
  }
  else {
    // We look at the zoom level and render a new page if the zoom has changed or
    // still has the value of `0.0` set by the constructor.
    if ( (_zoomLevel != scaleFactor) && not _pageIsRendering )
    {
      // Indicate that a render is in progress so that subsequent paint events
      // won't trigger a re-render. Once `_pageImageGenerator` emits a `finished`
      // signal, this boolean is cleared.
      _pageIsRendering = true;

      if (docScene) {
        // Connect the request object's signal to this object to receive a
        // notification when the finished page is available.
        // Note: We don't need to disconnect, as that is handled by Qt
        // automatically when the request object is destroyed later on
        // Note: addWorkItem must be separate, as otherwise the processing might
        // finish before we have actually finished initialization (i.e.,
        // finished connecting to the signal)
        PageProcessingRenderPageRequest * request = docScene->processingThread().requestRenderPage(this, scaleFactor);
        if (request) {
          connect(request, SIGNAL(pageImageReady(qreal, QImage)), this, SLOT(updateRenderedPage(qreal, QImage)));
          docScene->processingThread().addPageProcessingRequest(request);
        }
      }

      _zoomLevel = scaleFactor;
    }
    if ( _pageIsRendering ) {
      // A new resized page is still rendering, so we "blow up" our current
      // render and paint that. For performance reasons, we store this so we don't
      // need to recreate it in every paint event.
      // Note: Creating the scaled pixmap can take some time at high
      // magnifications, as can copying the fully rendered page from the rendering
      // thread into _renderedPage. Both cause a small lag on the UI.
      // **TODO:** Investigate if it would help to only move pointers around. In
      // this case, we'd have to take care which thread an image belongs to before
      // assigning/deallocating.
      QSizeF scaledSize = painter->transform().mapRect(boundingRect()).size();

      if (_temporaryPage.isNull() || _temporaryPage.size() != scaledSize.toSize())
        _temporaryPage = _renderedPage.scaled(scaledSize.toSize());

      painter->setTransform(QTransform());
      painter->drawPixmap(origin, _temporaryPage);
    } else {
      painter->setTransform(QTransform());
      painter->drawPixmap(origin, _renderedPage);
    }
  }
}


// This method causes the `PDFPageGraphicsItem` to take ownership of
// asynchronously generated `PDFLinkGraphicsItem` objects. Calling
// `setParentItem` causes the link objects to be added to the scene that owns
// the page object. `update` is then called to ensure all links are drawn at
// once.
void PDFPageGraphicsItem::addLinks(QList<PDFLinkGraphicsItem *> links)
{
  foreach( PDFLinkGraphicsItem *item, links ) item->setParentItem(this);

  update();
}


// Asynchronous Page Rendering
// ---------------------------

void PDFPageGraphicsItem::updateRenderedPage(qreal scaleFactor, QImage pageImage)
{
  // We store the rendered page in a new member named `renderedPage` rather
  // than the `pixmap` member inherited from `QGraphicsPixmapItem`. This is
  // because the size of `pixmap` is used to calculate a bunch of geometric
  // attributes for `QGraphicsPixmapItem`. When the page is re-rendered, we
  // just want to increase the resolution, not affect the geometry of the item
  // in the graphics scene.
  _renderedPage = QPixmap::fromImage(pageImage);

  // Since we have the fully rendered page now, we don't need any temporarily
  // scaled version anymore
  if (!_temporaryPage.isNull())
    _temporaryPage = QPixmap();

  // Indicate that page rendering has completed and this item needs to be
  // re-drawn.
  _pageIsRendering = false;
  update();
}

void PDFPageGraphicsItem::updateMagnifiedPage(qreal scaleFactor, QImage pageImage)
{
  // We store the rendered page in a new member named `renderedPage` rather
  // than the `pixmap` member inherited from `QGraphicsPixmapItem`. This is
  // because the size of `pixmap` is used to calculate a bunch of geometric
  // attributes for `QGraphicsPixmapItem`. When the page is re-rendered, we
  // just want to increase the resolution, not affect the geometry of the item
  // in the graphics scene.
  _magnifiedPage = QPixmap::fromImage(pageImage);

  // Since we have the fully rendered page now, we don't need any temporarily
  // scaled version anymore
  if (!_temporaryMagnifiedPage.isNull())
    _temporaryMagnifiedPage = QPixmap();

  // Indicate that page rendering has completed and this item needs to be
  // re-drawn.
  _pageIsRendering = false;
  update();
}


// PDFLinkGraphicsItem
// ===================

// This class descends from `QGraphicsRectItem` and serves the following
// functions:
//
//    * Provides easy access to the on-screen geometry of a hyperlink area.
//
//    * Handles tasks such as cursor changes on mouse hover and link activation
//      on mouse clicks.
PDFLinkGraphicsItem::PDFLinkGraphicsItem(Poppler::Link *a_link, QGraphicsItem *parent):
  Super(parent),
  _link(a_link),
  _activated(false)
{
  // Poppler expresses the link area in "normalized page coordinates", i.e.
  // values in the range [0, 1]. The transformation matrix of this item will
  // have to be adjusted so that links will show up correctly in a graphics
  // view.
  setRect(_link->linkArea());

  // Allows links to provide a context-specific cursor when the mouse is
  // hovering over them.
  //
  // **NOTE:** _Requires Qt 4.4 or newer._
  setAcceptHoverEvents(true);

  // Only left-clicks will trigger the link.
  setAcceptedMouseButtons(Qt::LeftButton);

#ifdef DEBUG
  // **TODO:**
  // _Currently for debugging purposes only so that the link area can be
  // determined visually, but might make a nice option._
  setPen(QPen(Qt::red));
#else
  // Perhaps there is a way to not draw the outline at all? Might be more
  // efficient...
  setPen(QPen(Qt::transparent));
#endif

  // Set some meaningful tooltip to inform the user what the link does
  // Using <p>...</p> ensures the tooltip text is interpreted as rich text
  // and thus is wrapping sensibly to avoid over-long lines.
  // Using PDFDocumentView::trUtf8 avoids having to explicitly derive
  // PDFLinkGraphicsItem explicily from QObject and puts all translatable
  // strings into the same context.
  switch(_link->linkType()) {
    case Poppler::Link::Goto:
      Poppler::LinkGoto * linkGoto;
      linkGoto = reinterpret_cast<Poppler::LinkGoto*>(_link);
      if (!linkGoto->isExternal())
        setToolTip(PDFDocumentView::trUtf8("<p>Goto page %1</p>").arg(linkGoto->destination().pageNumber()));
      else
        //: Example: "Goto page 5 of abc.pdf"
        setToolTip(PDFDocumentView::trUtf8("<p>Goto page %1 of %2</p>").arg(linkGoto->destination().pageNumber()).arg(linkGoto->fileName()));
      break;
    case Poppler::Link::Execute:
      Poppler::LinkExecute * linkExecute;
      linkExecute = reinterpret_cast<Poppler::LinkExecute*>(_link);
      if (linkExecute->parameters().isEmpty())
        setToolTip(PDFDocumentView::trUtf8("<p>Execute `%1`</p>"));
      else
        //: Example: "Execute `ls -1`"
        setToolTip(PDFDocumentView::trUtf8("<p>Execute `%1 %2`</p>"));
      break;
    case Poppler::Link::Browse:
      Poppler::LinkBrowse * linkBrowse;
      linkBrowse = reinterpret_cast<Poppler::LinkBrowse*>(_link);
      setToolTip(QString::fromUtf8("<p>%1</p>").arg(linkBrowse->url()));
      break;
      // Unsupported link types
//    case Poppler::Link::Action:
//    case Poppler::Link::Sound:
//    case Poppler::Link::Movie:
//    case Poppler::Link::JavaScript:
//    case Poppler::Link::None:
    default:
      break;
  }
}

int PDFLinkGraphicsItem::type() const { return Type; }

// Event Handlers
// --------------

// Swap cursor during hover events.
void PDFLinkGraphicsItem::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
  setCursor(Qt::PointingHandCursor);
}

void PDFLinkGraphicsItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
  unsetCursor();
}

// Respond to clicks. Limited to left-clicks by `setAcceptedMouseButtons` in
// this object's constructor.
void PDFLinkGraphicsItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
  // Actually opening the link is handled during a `mouseReleaseEvent` --- but
  // only if the `_activated` flag is `true`.
  _activated = true;
}

// The real nitty-gritty of link activation happens in here.
void PDFLinkGraphicsItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
  // Check that this link was "activated" (mouse press occurred within the link
  // bounding box) and that the mouse release also occurred within the bounding
  // box.
  if ( (not _activated) || (not contains(event->pos())) )
  {
    _activated = false;
    return;
  }

  switch ( _link->linkType() )
  {

    case Poppler::Link::Goto:
    {
      const Poppler::LinkGoto *target = dynamic_cast<const Poppler::LinkGoto*>(_link);
      Q_ASSERT(target != NULL);

      // **FIXME:** _We don't handle this yet!_
      if ( target->isExternal() ) break;

      // Jump by page number. Links reckon page numbers starting with 1 so we
      // subtract to conform with 0-based indexing used by C++.
      //
      // **NOTE:**
      // _There are many details that are not being considered, such as
      // centering on a specific anchor point and possibly changing the zoom
      // level rather than just focusing on the center of the target page._
      const int destPage = target->destination().pageNumber() - 1;

      // Post an event to the parent scene. The scene then takes care of
      // notifying objects, such as `PDFDocumentView`, that may want to take
      // action via a `SIGNAL`.
      QCoreApplication::postEvent(scene(), new PDFLinkEvent(destPage));
      break;
    }

    // Unsupported link types:
    //
    //     Poppler::Link::None
    //     Poppler::Link::Browse
    //     Poppler::Link::Execute
    //     Poppler::Link::JavaScript
    //     Poppler::Link::Action
    //     Poppler::Link::Sound
    //     Poppler::Link::Movie
    default:
      break;
  }

  _activated = false;
}


// PDFLinkEvent
// ============

// A PDF Link event is generated when a link is clicked and contains the page
// number of the link target.
PDFLinkEvent::PDFLinkEvent(int a_page) : Super(LinkEvent), pageNum(a_page) {}

// Obtain a unique ID for `PDFLinkEvent` that can be used by event handlers to
// filter out these events.
QEvent::Type PDFLinkEvent::LinkEvent = static_cast<QEvent::Type>( QEvent::registerEventType() );

PDFPageProcessingThread::PDFPageProcessingThread() :
_quit(false)
{
}

PDFPageProcessingThread::~PDFPageProcessingThread()
{
  _mutex.lock();
  _quit = true;
  _waitCondition.wakeAll();
  _mutex.unlock();
  wait();
}

PageProcessingRenderPageRequest * PDFPageProcessingThread::requestRenderPage(PDFPageGraphicsItem * page, qreal scaleFactor) const
{
  return new PageProcessingRenderPageRequest(page, scaleFactor);
}

PageProcessingLoadLinksRequest* PDFPageProcessingThread::requestLoadLinks(PDFPageGraphicsItem * page) const
{
  return new PageProcessingLoadLinksRequest(page);
}

void PDFPageProcessingThread::addPageProcessingRequest(PageProcessingRequest * request)
{
  int i;

  if (!request)
    return;

  QMutexLocker locker(&(this->_mutex));
  // remove any instances of the given request type before adding the new one to
  // avoid processing it several times
  // **TODO:** Could it be that we require several concurrent versions of the
  //           same page?
  for (i = _workStack.size() - 1; i >= 0; --i) {
    if (_workStack[i]->page == request->page && _workStack[i]->type() == request->type()) {
      // Using deleteLater() doesn't work because we have no event queue in this
      // thread. However, since the object is still on the stack, it is still
      // sleeping and directly deleting it should therefore be safe.
      delete _workStack[i];
      _workStack.remove(i);
    }
  }

  _workStack.push(request);
  locker.unlock();
#ifdef DEBUG
  QString jobDesc;
  switch (request->type()) {
    case PageProcessingRequest::LoadLinks:
      jobDesc = QString::fromUtf8("loading links request");
      break;
    case PageProcessingRequest::PageRendering:
      jobDesc = QString::fromUtf8("rendering page request");
      break;
  }
  qDebug() << "new" << jobDesc << "for page" << qobject_cast<PDFDocumentScene*>(request->page->scene())->pageNumFor(request->page) << "added to stack; now has" << _workStack.size() << "items";
#endif

  if (!isRunning())
    start();
  else
    _waitCondition.wakeOne();
}

void PDFPageProcessingThread::run()
{
  PageProcessingRequest * workItem;

  _mutex.lock();
  while (!_quit) {
    // mutex must be locked at start of loop
    if (_workStack.size() > 0) {
      workItem = _workStack.pop();
      _mutex.unlock();

#ifdef DEBUG
      qDebug() << "processing work item; remaining items:" << _workStack.size();
      _renderTimer.start();
#endif
      workItem->execute();
#ifdef DEBUG
      QString jobDesc;
      switch (workItem->type()) {
        case PageProcessingRequest::LoadLinks:
          jobDesc = QString::fromUtf8("loading links");
          break;
        case PageProcessingRequest::PageRendering:
          jobDesc = QString::fromUtf8("rendering page");
          break;
      }
      qDebug() << "finished " << jobDesc << "for page" << qobject_cast<PDFDocumentScene*>(workItem->page->scene())->pageNumFor(workItem->page) << "; time elapsed:" << _renderTimer.elapsed() << "ms";
#endif

      // Delete the work item as it has fulfilled its purpose
      // Note that we can't delete it here or we might risk that some emitted
      // signals are invalidated; to ensure they reach their destination, we
      // need to call deleteLater(), which requires and event queue; thus, we
      // first move it to the main processing thread
      workItem->moveToThread(QApplication::instance()->thread());
      workItem->deleteLater();

      _mutex.lock();
    }
    else {
#ifdef DEBUG
      qDebug() << "going to sleep";
#endif
      _waitCondition.wait(&_mutex);
#ifdef DEBUG
      qDebug() << "waking up";
#endif
    }
  }
}

PDFPageLayout::PDFPageLayout() :
_numCols(1),
_firstCol(0),
_xSpacing(10),
_ySpacing(10),
_isContinuous(true)
{
}

void PDFPageLayout::setColumnCount(const int numCols) {
  // We need at least one column, and we only handle changes
  if (numCols <= 0 || numCols == _numCols)
    return;

  _numCols = numCols;
  // Make sure the first column is still valid
  if (_firstCol >= _numCols)
    _firstCol = _numCols - 1;
  rearrange();
}

void PDFPageLayout::setColumnCount(const int numCols, const int firstCol) {
  // We need at least one column, and we only handle changes
  if (numCols <= 0 || (numCols == _numCols && firstCol == _firstCol))
    return;

  _numCols = numCols;

  if (firstCol < 0)
    _firstCol = 0;
  else if (firstCol >= _numCols)
    _firstCol = _numCols - 1;
  else
    _firstCol = firstCol;
  rearrange();
}

void PDFPageLayout::setFirstColumn(const int firstCol) {
  // We only handle changes
  if (firstCol == _firstCol)
    return;

  if (firstCol < 0)
    _firstCol = 0;
  else if (firstCol >= _numCols)
    _firstCol = _numCols - 1;
  else
    _firstCol = firstCol;
  rearrange();
}

void PDFPageLayout::setXSpacing(const qreal xSpacing) {
  if (xSpacing > 0)
    _xSpacing = xSpacing;
  else
    _xSpacing = 0.;
}

void PDFPageLayout::setYSpacing(const qreal ySpacing) {
  if (ySpacing > 0)
    _ySpacing = ySpacing;
  else
    _ySpacing = 0.;
}

void PDFPageLayout::setContinuous(const bool continuous /* = true */)
{
  if (continuous == _isContinuous)
    return;
  _isContinuous = continuous;
  if (!_isContinuous)
    setColumnCount(1, 0);
    // setColumnCount() calls relayout automatically
  else relayout();
}

int PDFPageLayout::rowCount() const {
  if (_layoutItems.isEmpty())
    return 0;
  return _layoutItems.last().row + 1;
}

void PDFPageLayout::addPage(PDFPageGraphicsItem * page) {
  LayoutItem item;

  if (!page)
    return;

  item.page = page;
  if (_layoutItems.isEmpty()) {
    item.row = 0;
    item.col = _firstCol;
  }
  else if (_layoutItems.last().col < _numCols - 1){
    item.row = _layoutItems.last().row;
    item.col = _layoutItems.last().col + 1;
  }
  else {
    item.row = _layoutItems.last().row + 1;
    item.col = 0;
  }
  _layoutItems.append(item);
}

void PDFPageLayout::removePage(PDFPageGraphicsItem * page) {
  QList<LayoutItem>::iterator it;
  int row, col;

  // **TODO:** Decide what to do with pages that are in the list multiple times
  // (see also insertPage())

  // First, find the page and remove it
  for (it = _layoutItems.begin(); it != _layoutItems.end(); ++it) {
    if (it->page == page) {
      row = it->row;
      col = it->col;
      it = _layoutItems.erase(it);
      break;
    }
  }

  // Then, rearrange the pages behind it (no call to rearrange() to save time
  // by not going over the unchanged pages in front of the removed one)
  for (; it != _layoutItems.end(); ++it) {
    it->row = row;
    it->col = col;

    ++col;
    if (col >= _numCols) {
      col = 0;
      ++row;
    }
  }
}

void PDFPageLayout::insertPage(PDFPageGraphicsItem * page, PDFPageGraphicsItem * before /* = NULL */) {
  QList<LayoutItem>::iterator it;
  int row, col;
  LayoutItem item;

  item.page = page;

  // **TODO:** Decide what to do with pages that are in the list multiple times
  // (see also insertPage())

  // First, find the page to insert before and insert (row and col will be set
  // below)
  for (it = _layoutItems.begin(); it != _layoutItems.end(); ++it) {
    if (it->page == before) {
      row = it->row;
      col = it->col;
      it = _layoutItems.insert(it, item);
      break;
    }
  }
  if (it == _layoutItems.end()) {
    // We haven't found "before", so we just append the page
    addPage(page);
    return;
  }

  // Then, rearrange the pages starting from the inserted one (no call to
  // rearrange() to save time by not going over the unchanged pages)
  for (; it != _layoutItems.end(); ++it) {
    it->row = row;
    it->col = col;

    ++col;
    if (col >= _numCols) {
      col = 0;
      ++row;
    }
  }
}

// Relayout the pages on the canvas
// **TODO:** Accessing Poppler::Page::pageSizeF() doesn't seem to pose a
// threading problem; at least it can be done while rendering in the background
// without acquiring the docMutex.
// Maybe we should use a QReadWriteLock instead of docMutex?
void PDFPageLayout::relayout() {
  if (_isContinuous)
    continuousModeRelayout();
  else
    singlePageModeRelayout();
}

// Relayout the pages on the canvas in continuous mode
// **TODO:** Accessing Poppler::Page::pageSizeF() doesn't seem to pose a
// threading problem; at least it can be done while rendering in the background
// without acquiring the docMutex.
// Maybe we should use a QReadWriteLock instead of docMutex?
void PDFPageLayout::continuousModeRelayout() {
  // Create arrays to hold offsets and make sure that they have
  // sufficient space (to avoid moving the data around in memory)
  QVector<qreal> colOffsets(_numCols + 1, 0), rowOffsets(rowCount() + 1, 0);
  int i;
  qreal x, y;
  QList<LayoutItem>::iterator it;
  PDFPageGraphicsItem * page;
  QSizeF pageSize;
  QRectF sceneRect;

  // First, fill the offsets with the respective widths and heights
  for (it = _layoutItems.begin(); it != _layoutItems.end(); ++it) {
    if (!it->page || !it->page->_page)
      continue;
    page = it->page;
    pageSize = page->_page->pageSizeF();

    if (colOffsets[it->col + 1] < pageSize.width() * page->_dpiX / 72.)
      colOffsets[it->col + 1] = pageSize.width() * page->_dpiX / 72.;
    if (rowOffsets[it->row + 1] < pageSize.height() * page->_dpiY / 72.)
      rowOffsets[it->row + 1] = pageSize.height() * page->_dpiY / 72.;
  }

  // Next, calculate cumulative offsets (including spacing)
  for (i = 1; i <= _numCols; ++i)
    colOffsets[i] += colOffsets[i - 1] + _xSpacing;
  for (i = 1; i <= rowCount(); ++i)
    rowOffsets[i] += rowOffsets[i - 1] + _ySpacing;

  // Finally, position pages
  // **TODO:** Figure out why this loop causes some noticable lag when switching
  // from SinglePage to continuous mode in a large document (but not when
  // switching between separate continuous modes)
  for (it = _layoutItems.begin(); it != _layoutItems.end(); ++it) {
    if (!it->page || !it->page->_page)
      continue;
    // If we have more than one column, right-align the left-most column and
    // left-align the right-most column to avoid large space between columns
    // In all other cases, center the page in allotted space (in case we
    // stumble over pages of different sizes, e.g., landscape pages, etc.)
    pageSize = it->page->_page->pageSizeF();
    if (_numCols > 1 && it->col == 0)
      x = colOffsets[it->col + 1] - _xSpacing - pageSize.width() * page->_dpiX / 72.;
    else if (_numCols > 1 && it->col == _numCols - 1)
      x = colOffsets[it->col];
    else
      x = 0.5 * (colOffsets[it->col + 1] + colOffsets[it->col] - _xSpacing - pageSize.width() * page->_dpiX / 72.);
    // Always center the page vertically
    y = 0.5 * (rowOffsets[it->row + 1] + rowOffsets[it->row] - _ySpacing - pageSize.height() * page->_dpiY / 72.);
    it->page->setPos(x, y);
  }

  // leave some space around the pages (note that the space on the right/bottom
  // is already included in the corresponding Offset values)
  sceneRect.setRect(-_xSpacing, -_ySpacing, colOffsets[_numCols] + _xSpacing, rowOffsets[rowCount()] + _ySpacing);
  emit layoutChanged(sceneRect);
}

// Relayout the pages on the canvas in single page mode
// **TODO:** Accessing Poppler::Page::pageSizeF() doesn't seem to pose a
// threading problem; at least it can be done while rendering in the background
// without acquiring the docMutex.
// Maybe we should use a QReadWriteLock instead of docMutex?
void PDFPageLayout::singlePageModeRelayout()
{
  qreal width, height, maxWidth = 0.0, maxHeight = 0.0;
  QList<LayoutItem>::iterator it;
  PDFPageGraphicsItem * page;
  QSizeF pageSize;
  QRectF sceneRect;

  // We lay out all pages such that their center is in the origin (since only
  // one page is visible at any time, this is no problem)
  for (it = _layoutItems.begin(); it != _layoutItems.end(); ++it) {
    if (!it->page || !it->page->_page)
      continue;
    page = it->page;
    pageSize = page->_page->pageSizeF();
    width = pageSize.width() * page->_dpiX / 72.;
    height = pageSize.height() * page->_dpiY / 72.;
    if (width > maxWidth)
      maxWidth = width;
    if (height > maxHeight)
      maxHeight = height;
    page->setPos(-width / 2., -height / 2.);
  }

  sceneRect.setRect(-maxWidth / 2., -maxHeight / 2., maxWidth, maxHeight);
  emit layoutChanged(sceneRect);
}

void PDFPageLayout::rearrange() {
  QList<LayoutItem>::iterator it;
  int row, col;

  row = 0;
  col = _firstCol;
  for (it = _layoutItems.begin(); it != _layoutItems.end(); ++it) {
    it->row = row;
    it->col = col;

    ++col;
    if (col >= _numCols) {
      col = 0;
      ++row;
    }
  }
}

PageProcessingRenderPageRequest::PageProcessingRenderPageRequest(PDFPageGraphicsItem * page, qreal scaleFactor) :
  PageProcessingRequest(page),
  scaleFactor(scaleFactor)
{
}

bool PageProcessingRenderPageRequest::execute()
{
  if (!page || !qobject_cast<PDFDocumentScene *>(page->scene()) || !page->_page)
    return false;

  QMutexLocker docLock(qobject_cast<PDFDocumentScene *>(page->scene())->docMutex);
  QImage pageImage = page->_page->renderToImage(page->_dpiX * scaleFactor, page->_dpiY * scaleFactor);
  docLock.unlock();
  
  emit pageImageReady(scaleFactor, pageImage);
  return true;
}

// Asynchronous Link Generation
// ----------------------------

// This function generates `PDFLinkGraphicsItem` objects. It is intended to be
// called asynchronously and so does not set parentage for the objects it
// generates --- this task is left to the `addLinks` method so that all the
// links are added and rendered in a synchronous operation.
bool PageProcessingLoadLinksRequest::execute()
{
  if (!page || !qobject_cast<PDFDocumentScene *>(page->scene()) || !page->_page)
    return false;

  // **TODO:**
  //
  //   * _Comment on how `pageScale` works and is used._

  // We need to acquire a mutex from `PDFDocumentScene` as accessing page data,
  // such as reading link lists or rendering page images is not thread safe
  // among pages objects created from the same document object.
  QMutexLocker docLock(qobject_cast<PDFDocumentScene *>(page->scene())->docMutex);
    QList<Poppler::Link *> links = page->_page->links();
  docLock.unlock();

  QList<PDFLinkGraphicsItem *> linkList;
  if( !links.isEmpty() ) {
    PDFLinkGraphicsItem *linkItem;

    foreach( Poppler::Link *link, links )
    {
      linkItem = new PDFLinkGraphicsItem(link);
      linkItem->setTransform(page->_pageScale);

      linkList.append(linkItem);
    }
  }

  emit linksReady(linkList);
  return true;
}


// vim: set sw=2 ts=2 et

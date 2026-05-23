#ifndef UIKIT_CONTAINER_H
#define UIKIT_CONTAINER_H

#include <uikit/widget.h>
#include <uikit/children.h>

#define UI_WIDGET_GRID   "@uikit/grid"
#define UI_WIDGET_SCROLL "@uikit/scroll"

// ---------------------------------------------------------------------
// UIGrid
// ---------------------------------------------------------------------
// Lays children out in a column-major grid. The grid itself has a
// fixed width/height (set via widgcs); each cell has fixed dimensions
// `cellW x cellH`. Items are placed left-to-right, top-to-bottom in
// row-major order starting from the grid's top-left. There is no
// auto-sizing of children - each child should be sized to match
// (cellW, cellH) or smaller.

/**
 * Fixed-cell grid container. Lays its children in row-major order
 * starting from the top-left, with `columns` columns and
 * `cellW x cellH` cells separated by `gapX` / `gapY`. Children should
 * be sized to fit a cell.
 */
typedef struct {
    const char* __widget_type; /**< Widget type tag (== UI_WIDGET_GRID). */

    float marginLeft;          /**< Left outer margin (pixels). */
    float marginTop;           /**< Top outer margin (pixels). */
    float marginRight;         /**< Right outer margin (pixels). */
    float marginBottom;        /**< Bottom outer margin (pixels). */

    int columns;               /**< Number of columns, >= 1. */
    float gapX;                /**< Horizontal spacing between cells. */
    float gapY;                /**< Vertical spacing between cells. */
    float cellW;               /**< Cell width in pixels. */
    float cellH;               /**< Cell height in pixels. */

    float paddingLeft;         /**< Inner left padding (pixels). */
    float paddingTop;          /**< Inner top padding (pixels). */
    float paddingRight;        /**< Inner right padding (pixels). */
    float paddingBottom;       /**< Inner bottom padding (pixels). */

    /**
     * Items shown in the grid. The grid OWNS this collection and frees
     * it (and the items) on Destroy.
     */
    UIChildren* items;
} UIGrid;

/**
 * Creates a grid with the given column count and reasonable defaults
 * (gap = 8, cell = 100x100, empty items list).
 */
UIGrid* UIGrid_Create(int columns);

UIGrid* UIGrid_SetColumns (UIGrid* g, int columns);
UIGrid* UIGrid_SetGap     (UIGrid* g, float gapX, float gapY);
UIGrid* UIGrid_SetCellSize(UIGrid* g, float w, float h);
UIGrid* UIGrid_SetPadding (UIGrid* g, float l, float t, float r, float b);

/**
 * Adds an item to the end of the grid. Returns 1 on success.
 * The item is added to the grid's internal UIChildren list, which
 * means the grid manages its lifetime.
 */
int UIGrid_AddItem(UIGrid* g, UIWidget* item);

/**
 * Returns the total content size of the grid based on item count
 * + columns + cellW/cellH + gap + padding. Useful when wrapping the
 * grid in a UIScroll.
 */
void UIGrid_GetContentSize(UIGrid* g, float* outW, float* outH);

void UIGrid_Destroy(UIGrid* g);


// ---------------------------------------------------------------------
// UIScroll
// ---------------------------------------------------------------------
// Viewport widget. Renders a single child (`content`) at an offset
// (-scrollX, -scrollY) inside the scroll's bounds, with a clip rect so
// nothing escapes. Mouse wheel scrolls vertically (with shift = horiz)
// and drag (when SetDragScroll(1) is enabled) lets you pan with the
// left button.

/**
 * Scrolling viewport widget. Renders a single owned `content` widget
 * at offset (-scrollX, -scrollY) inside the scroll's bounds, with a
 * clip rect so the content never escapes. Supports mouse-wheel
 * scrolling and optional drag-to-pan.
 */
typedef struct {
    const char* __widget_type; /**< Widget type tag (== UI_WIDGET_SCROLL). */

    float marginLeft;          /**< Left outer margin (pixels). */
    float marginTop;           /**< Top outer margin (pixels). */
    float marginRight;         /**< Right outer margin (pixels). */
    float marginBottom;        /**< Bottom outer margin (pixels). */

    /**
     * The widget rendered inside the viewport. The scroll OWNS this
     * pointer - it is destroyed by UIScroll_Destroy.
     */
    UIWidget* content;

    float scrollX;             /**< Current horizontal scroll offset (pixels). */
    float scrollY;             /**< Current vertical scroll offset (pixels). */
    float contentW;            /**< Cached content width (measured on first render). */
    float contentH;            /**< Cached content height. */

    int allowVertical;         /**< 1 enables vertical scrolling via wheel/drag. */
    int allowHorizontal;       /**< 1 enables horizontal scrolling (shift+wheel). */
    int allowDragScroll;       /**< 1 lets the user pan with the left mouse button. */
    int isDragging;            /**< Internal: 1 while a drag-pan is in progress. */

    float wheelSpeed;           /**< Pixels per wheel notch. Default 60. */

    /** Optional background drawn behind the content (NULL = none). */
    void* background;           /**< UIRectangle* */
} UIScroll;

UIScroll* UIScroll_Create(void);

/**
 * Sets the single content widget rendered inside the viewport. The
 * scroll takes ownership; it will destroy `content` together with
 * itself. Pass NULL to clear (and destroy the previous content).
 */
UIScroll* UIScroll_SetContent(UIScroll* s, UIWidget* content);

UIScroll* UIScroll_SetScroll      (UIScroll* s, float x, float y);
UIScroll* UIScroll_SetAxes        (UIScroll* s, int allowVertical, int allowHorizontal);
UIScroll* UIScroll_SetDragScroll  (UIScroll* s, int enabled);
UIScroll* UIScroll_SetWheelSpeed  (UIScroll* s, float pxPerNotch);

/**
 * Manually invalidates the cached content size. Call this when the
 * content (e.g. a grid) had items added/removed after being set as
 * content. Most callers won't need it - the scroll measures on first
 * render.
 */
void UIScroll_InvalidateContentSize(UIScroll* s);

void UIScroll_Destroy(UIScroll* s);

// ---------------------------------------------------------------------
// UIListView / UIGridView - sugar that wires Scroll + Grid together
// ---------------------------------------------------------------------
// These return a UIScroll (already populated with a UIGrid as content).
// Add items via UIListView_AddItem / UIGridView_AddItem - they forward
// to the embedded grid.

UIScroll* UIListView_Create(float itemHeight);
int       UIListView_AddItem(UIScroll* listView, UIWidget* item);
UIGrid*   UIListView_GetGrid(UIScroll* listView);

UIScroll* UIGridView_Create(int columns, float cellW, float cellH);
int       UIGridView_AddItem(UIScroll* gridView, UIWidget* item);
UIGrid*   UIGridView_GetGrid(UIScroll* gridView);

#endif // UIKIT_CONTAINER_H

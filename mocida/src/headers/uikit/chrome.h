#ifndef UIKIT_CHROME_H
#define UIKIT_CHROME_H

#include <SDL3/SDL.h>
#include <uikit/widget.h>
#include <uikit/children.h>
#include <uikit/color.h>
#include <uikit/cursor.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_WIDGET_SEGMENTED "@uikit/segmented"
#define UI_WIDGET_TABBAR    "@uikit/tabbar"
#define UI_WIDGET_NAVBAR    "@uikit/navbar"

#define UI_CHROME_MAX_ITEMS 16

typedef struct UIChromeItem {
    char* title;           /**< Heap-owned label (may be empty). */
    char* icon;            /**< Heap-owned image path, or NULL. */
    char* symbol;          /**< Heap-owned glyph / emoji, or NULL. */
    char* value;           /**< Optional string value (NavBar → page id). */
    SDL_Texture* iconTex;  /**< Lazy-loaded icon texture (owned). */
    SDL_Texture* titleTex; /**< Cached label texture (owned). */
    SDL_Texture* symbolTex;/**< Cached symbol texture (owned). */
    int titleLenCached;
    int symbolLenCached;
    int selectedCached;    /**< 1 when titleTex was built as selected. */
} UIChromeItem;

/* ------------------------------------------------------------------ */
/* SegmentedControl — pill track with equal-width segments            */
/* ------------------------------------------------------------------ */

typedef struct UISegmentedControl UISegmentedControl;
typedef void (*UISegmentedCallback)(UISegmentedControl* sc, int index, void* userdata);

struct UISegmentedControl {
    const char* __widget_type;
    int selected;
    int hovered;       /**< Segment under cursor, or -1. */
    int pressed;       /**< Segment pressed, or -1. */
    float _phase;      /**< Animated selected index (for sliding pill). */
    int animMs;
    float radius;
    float padding;     /**< Inner inset of the sliding pill. */
    float iconSize;
    float gap;         /**< Gap between icon/symbol and label. */
    UICursor cursor;

    UIColor trackBg;
    UIColor selectedBg;
    UIColor textColor;
    UIColor selectedTextColor;
    UIColor borderColor;
    float borderWidth;

    char* fontFamily;
    float fontSize;

    UIChromeItem items[UI_CHROME_MAX_ITEMS];
    int itemCount;

    UISegmentedCallback onChange;
    void* userdata;
};

UISegmentedControl* UISegmentedControl_Create(void);
int  UISegmentedControl_AddSegment(UISegmentedControl* sc,
                                   const char* title,
                                   const char* icon,
                                   const char* symbol);
UISegmentedControl* UISegmentedControl_SetSelected(UISegmentedControl* sc, int index);
int  UISegmentedControl_Selected(const UISegmentedControl* sc);
UISegmentedControl* UISegmentedControl_SetColors(UISegmentedControl* sc,
                                                 UIColor track, UIColor selected,
                                                 UIColor text, UIColor selectedText);
UISegmentedControl* UISegmentedControl_SetBorder(UISegmentedControl* sc,
                                                 UIColor color, float width);
UISegmentedControl* UISegmentedControl_SetRadius(UISegmentedControl* sc, float radius);
UISegmentedControl* UISegmentedControl_SetAnimMs(UISegmentedControl* sc, int ms);
UISegmentedControl* UISegmentedControl_SetFont(UISegmentedControl* sc,
                                               char* family, float size);
UISegmentedControl* UISegmentedControl_SetIconSize(UISegmentedControl* sc, float size);
UISegmentedControl* UISegmentedControl_SetCursor(UISegmentedControl* sc, UICursor cursor);
UISegmentedControl* UISegmentedControl_OnChange(UISegmentedControl* sc,
                                                UISegmentedCallback cb, void* userdata);
void UISegmentedControl_Destroy(UISegmentedControl* sc);
void UISegmentedControl_DispatchMouseMotion(UIChildren* children, float x, float y);
void UISegmentedControl_DispatchMouseDown(UIChildren* children, float x, float y, int button);
void UISegmentedControl_DispatchMouseUp(UIChildren* children, float x, float y, int button);

/* ------------------------------------------------------------------ */
/* TabBar — bottom (or top) icon + label bar                          */
/* ------------------------------------------------------------------ */

typedef struct UITabBar UITabBar;
typedef void (*UITabBarCallback)(UITabBar* tb, int index, void* userdata);

struct UITabBar {
    const char* __widget_type;
    int selected;
    int hovered;
    int pressed;
    float iconSize;
    float gap;
    UICursor cursor;

    UIColor barBg;
    UIColor selectedTint;
    UIColor unselectedTint;
    UIColor borderColor;
    float borderWidth;
    float radius;

    char* fontFamily;
    float fontSize;

    UIChromeItem items[UI_CHROME_MAX_ITEMS];
    int itemCount;

    UITabBarCallback onChange;
    void* userdata;
};

UITabBar* UITabBar_Create(void);
int  UITabBar_AddItem(UITabBar* tb, const char* title, const char* icon, const char* symbol);
UITabBar* UITabBar_SetSelected(UITabBar* tb, int index);
int  UITabBar_Selected(const UITabBar* tb);
UITabBar* UITabBar_SetColors(UITabBar* tb, UIColor bar, UIColor selected, UIColor unselected);
UITabBar* UITabBar_SetBorder(UITabBar* tb, UIColor color, float width);
UITabBar* UITabBar_SetRadius(UITabBar* tb, float radius);
UITabBar* UITabBar_SetFont(UITabBar* tb, char* family, float size);
UITabBar* UITabBar_SetIconSize(UITabBar* tb, float size);
UITabBar* UITabBar_SetCursor(UITabBar* tb, UICursor cursor);
UITabBar* UITabBar_OnChange(UITabBar* tb, UITabBarCallback cb, void* userdata);
void UITabBar_Destroy(UITabBar* tb);
void UITabBar_DispatchMouseMotion(UIChildren* children, float x, float y);
void UITabBar_DispatchMouseDown(UIChildren* children, float x, float y, int button);
void UITabBar_DispatchMouseUp(UIChildren* children, float x, float y, int button);

/* ------------------------------------------------------------------ */
/* NavBar — top brand + center links + optional menu button           */
/* ------------------------------------------------------------------ */

typedef struct UINavBar UINavBar;
typedef void (*UINavBarCallback)(UINavBar* nb, int index, void* userdata);
typedef void (*UINavBarSimpleCallback)(UINavBar* nb, void* userdata);

struct UINavBar {
    const char* __widget_type;
    int selected;
    int hovered;       /**< Center item index, or -1. */
    int pressed;
    int menuHovered;
    int menuPressed;
    int menuVisible;   /**< Show trailing hamburger. */
    int menuOpen;      /**< Visual open state for the menu button. */
    float iconSize;
    float brandGap;
    UICursor cursor;

    UIColor barBg;
    UIColor titleColor;
    UIColor linkColor;
    UIColor linkActiveColor;
    UIColor linkHoverBg;
    UIColor menuBg;
    UIColor menuBgOpen;
    UIColor borderColor;
    float borderWidth;

    char* brandTitle;
    char* brandLogo;       /**< Image path or NULL. */
    SDL_Texture* logoTex;
    SDL_Texture* brandTex;
    int brandLenCached;

    char* fontFamily;
    float fontSize;
    float brandFontSize;

    UIChromeItem items[UI_CHROME_MAX_ITEMS];
    int itemCount;

    UINavBarCallback onChange;
    UINavBarSimpleCallback onBrandClick;
    UINavBarSimpleCallback onMenuClick;
    void* userdata;
    void* brandUserdata;
    void* menuUserdata;
};

UINavBar* UINavBar_Create(void);
UINavBar* UINavBar_SetBrand(UINavBar* nb, const char* title, const char* logo);
int  UINavBar_AddItem(UINavBar* nb, const char* title, const char* value);
UINavBar* UINavBar_SetSelected(UINavBar* nb, int index);
int  UINavBar_Selected(const UINavBar* nb);
UINavBar* UINavBar_SetMenuVisible(UINavBar* nb, int visible);
UINavBar* UINavBar_SetMenuOpen(UINavBar* nb, int open);
UINavBar* UINavBar_SetColors(UINavBar* nb, UIColor bar, UIColor title,
                             UIColor link, UIColor linkActive);
UINavBar* UINavBar_SetBorder(UINavBar* nb, UIColor color, float width);
UINavBar* UINavBar_SetFont(UINavBar* nb, char* family, float size, float brandSize);
UINavBar* UINavBar_SetCursor(UINavBar* nb, UICursor cursor);
UINavBar* UINavBar_OnChange(UINavBar* nb, UINavBarCallback cb, void* userdata);
UINavBar* UINavBar_OnBrandClick(UINavBar* nb, UINavBarSimpleCallback cb, void* userdata);
UINavBar* UINavBar_OnMenuClick(UINavBar* nb, UINavBarSimpleCallback cb, void* userdata);
void UINavBar_Destroy(UINavBar* nb);
void UINavBar_DispatchMouseMotion(UIChildren* children, float x, float y);
void UINavBar_DispatchMouseDown(UIChildren* children, float x, float y, int button);
void UINavBar_DispatchMouseUp(UIChildren* children, float x, float y, int button);

#ifdef __cplusplus
}
#endif

#endif /* UIKIT_CHROME_H */

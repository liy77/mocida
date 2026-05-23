#ifndef UIKIT_CONTROLS_H
#define UIKIT_CONTROLS_H

#include <uikit/widget.h>
#include <uikit/color.h>
#include <uikit/rect.h>
#include <uikit/children.h>
#include <uikit/cursor.h>

#define UI_WIDGET_CHECKBOX     "@uikit/checkbox"
#define UI_WIDGET_SLIDER       "@uikit/slider"
#define UI_WIDGET_PROGRESS_BAR "@uikit/progress_bar"
#define UI_WIDGET_SPINNER      "@uikit/spinner"
#define UI_WIDGET_SWITCH       "@uikit/switch"
#define UI_WIDGET_RADIO        "@uikit/radio"

// ---------------------------------------------------------------------
// UICheckbox - toggle widget with on/off state. Click anywhere inside
// the bounds to toggle. Visual: rounded box with a checkmark when on.
// ---------------------------------------------------------------------
typedef struct UICheckbox UICheckbox;
typedef void (*UICheckboxCallback)(UICheckbox* cb, int checked, void* userdata);

/**
 * Two-state toggle drawn as a rounded box. Click anywhere inside the
 * bounds to flip `checked`; the check mark eases in/out over `animMs`
 * milliseconds.
 */
struct UICheckbox {
    const char* __widget_type; /**< Widget type tag (== UI_WIDGET_CHECKBOX). */
    int checked;               /**< Current state (0 = unchecked, 1 = checked). */
    int hovered;               /**< Internal: mouse currently over the box. */
    int pressed;               /**< Internal: mouse-down without release. */
    UICursor cursor;           /**< Cursor shown on hover. */
    float radius;              /**< Corner radius of the outer box. */
    UIColor boxColor;          /**< Box fill color. */
    UIColor checkColor;        /**< Check-mark / fill highlight color. */
    UIColor borderColor;       /**< Border color. */
    float borderWidth;         /**< Border thickness (pixels). */

    int   animMs;              /**< Toggle animation duration in ms (0 = instant). */
    float _phase;              /**< Internal eased phase: 0 = unchecked, 1 = checked. */

    UICheckboxCallback onChange; /**< Fires when `checked` flips. */
    void* userdata;              /**< Opaque pointer forwarded to onChange. */
};

UICheckbox* UICheckbox_Create        (int initialChecked);
UICheckbox* UICheckbox_SetChecked    (UICheckbox* c, int checked);
UICheckbox* UICheckbox_SetColors     (UICheckbox* c, UIColor box, UIColor check);
UICheckbox* UICheckbox_SetBoxColor   (UICheckbox* c, UIColor color);
UICheckbox* UICheckbox_SetCheckColor (UICheckbox* c, UIColor color);
UICheckbox* UICheckbox_SetBorder     (UICheckbox* c, UIColor color, float width);
UICheckbox* UICheckbox_SetRadius     (UICheckbox* c, float radius);
UICheckbox* UICheckbox_SetAnimMs     (UICheckbox* c, int ms);
UICheckbox* UICheckbox_SetCursor     (UICheckbox* c, UICursor cursor);
UICheckbox* UICheckbox_OnChange      (UICheckbox* c, UICheckboxCallback cb, void* userdata);
void        UICheckbox_Destroy       (UICheckbox* c);

// ---------------------------------------------------------------------
// UISlider - drag a knob along a horizontal track to pick a value in
// [min, max]. The float value is exposed directly; you can also
// register an onChange callback for live updates.
// ---------------------------------------------------------------------
typedef struct UISlider UISlider;
typedef void (*UISliderCallback)(UISlider* s, float value, void* userdata);

/**
 * Horizontal slider with a draggable knob. The exposed `value` is
 * clamped to [minValue, maxValue]; `onChange` fires whenever the value
 * changes (either via drag or programmatic SetValue).
 */
struct UISlider {
    const char* __widget_type;  /**< Widget type tag (== UI_WIDGET_SLIDER). */
    float minValue;             /**< Lower bound of the slider range. */
    float maxValue;             /**< Upper bound of the slider range. */
    float value;                /**< Current value, always in [minValue, maxValue]. */
    int dragging;               /**< Internal: 1 while the user is dragging the knob. */
    UICursor cursor;            /**< Cursor shown on hover. */
    UIColor trackColor;         /**< Empty portion of the track. */
    UIColor fillColor;          /**< Filled portion (from min up to value). */
    UIColor knobColor;          /**< Knob fill color. */
    float trackHeight;          /**< Height of the track bar in pixels. */
    float knobRadius;           /**< Radius of the knob circle in pixels. */
    UISliderCallback onChange;  /**< Fires whenever `value` changes (drag or programmatic). */
    void* userdata;             /**< Opaque pointer forwarded to onChange. */
};

UISlider* UISlider_Create        (float minV, float maxV, float initialValue);
UISlider* UISlider_SetValue      (UISlider* s, float value);
UISlider* UISlider_SetRange      (UISlider* s, float minV, float maxV);
UISlider* UISlider_SetColors     (UISlider* s, UIColor track, UIColor fill, UIColor knob);
UISlider* UISlider_SetTrackColor (UISlider* s, UIColor color);
UISlider* UISlider_SetFillColor  (UISlider* s, UIColor color);
UISlider* UISlider_SetKnobColor  (UISlider* s, UIColor color);
UISlider* UISlider_SetTrackHeight(UISlider* s, float height);
UISlider* UISlider_SetKnobRadius (UISlider* s, float radius);
UISlider* UISlider_OnChange      (UISlider* s, UISliderCallback cb, void* userdata);
UISlider* UISlider_SetCursor     (UISlider* s, UICursor cursor);
void      UISlider_Destroy       (UISlider* s);

// ---------------------------------------------------------------------
// UIProgressBar - non-interactive read-only progress indicator. Value
// is in [0, 1]. When indeterminate is set, a moving bar travels back
// and forth (driven by the global animation tick).
// ---------------------------------------------------------------------
/**
 * Read-only progress indicator. `value` is in [0, 1]. When
 * `indeterminate` is non-zero a sliding bar animates back and forth
 * to signal ongoing work without a known endpoint.
 */
typedef struct {
    const char* __widget_type; /**< Widget type tag (== UI_WIDGET_PROGRESS_BAR). */
    float value;               /**< Progress in [0, 1]. */
    int indeterminate;         /**< Non-zero plays the back-and-forth animation. */
    float radius;              /**< Corner radius of the track / fill. */
    UIColor trackColor;        /**< Background track color. */
    UIColor fillColor;         /**< Progress fill color. */
    float _phase;              /**< Internal phase [0..1] for indeterminate animation. */
} UIProgressBar;

UIProgressBar* UIProgressBar_Create          (float initialValue);
UIProgressBar* UIProgressBar_SetValue        (UIProgressBar* p, float value);
UIProgressBar* UIProgressBar_SetIndeterminate(UIProgressBar* p, int yes);
UIProgressBar* UIProgressBar_SetColors       (UIProgressBar* p, UIColor track, UIColor fill);
UIProgressBar* UIProgressBar_SetTrackColor   (UIProgressBar* p, UIColor color);
UIProgressBar* UIProgressBar_SetFillColor    (UIProgressBar* p, UIColor color);
UIProgressBar* UIProgressBar_SetRadius       (UIProgressBar* p, float radius);
void           UIProgressBar_Destroy         (UIProgressBar* p);

// ---------------------------------------------------------------------
// UISpinner - circular loading indicator. Rotates over time driven by
// the global animation tick.
// ---------------------------------------------------------------------
/**
 * Circular busy spinner. Renders a partial arc that rotates over time
 * at `speed` radians per second. Use it together with operations whose
 * progress can't be measured.
 */
typedef struct {
    const char* __widget_type; /**< Widget type tag (== UI_WIDGET_SPINNER). */
    float radius;              /**< Outer radius of the spinning arc. */
    float thickness;           /**< Arc stroke width (pixels). */
    float speed;               /**< Rotation speed in rad/s; default ~2π (1 rev/s). */
    UIColor color;             /**< Arc color. */
    float _phase;              /**< Internal: current rotation in radians. */
} UISpinner;

UISpinner* UISpinner_Create      (float radius);
UISpinner* UISpinner_SetColor    (UISpinner* s, UIColor color);
UISpinner* UISpinner_SetThickness(UISpinner* s, float thickness);
UISpinner* UISpinner_SetRadius   (UISpinner* s, float radius);
UISpinner* UISpinner_SetSpeed    (UISpinner* s, float speedPerSec);
void       UISpinner_Destroy     (UISpinner* s);

// ---------------------------------------------------------------------
// UISwitch - boolean toggle drawn as a horizontal pill with a knob
// that slides between the off (left) and on (right) positions. Click
// anywhere inside the bounds to flip. The knob animates over
// `animMs` milliseconds (set 0 to disable for instant toggling).
// ---------------------------------------------------------------------
typedef struct UISwitch UISwitch;
typedef void (*UISwitchCallback)(UISwitch* sw, int on, void* userdata);

/**
 * Boolean toggle drawn as a pill with a sliding knob. Click anywhere
 * inside the bounds to flip `on`; the knob eases between the off and
 * on positions over `animMs` milliseconds.
 */
struct UISwitch {
    const char* __widget_type; /**< Widget type tag (== UI_WIDGET_SWITCH). */
    int on;                    /**< Current state (0 = off, 1 = on). */
    int hovered;               /**< Internal: mouse currently over the switch. */
    int pressed;               /**< Internal: mouse-down without release. */
    UICursor cursor;           /**< Cursor shown on hover. */

    UIColor offColor;          /**< Track background when off. */
    UIColor onColor;           /**< Track background when on. */
    UIColor knobColor;         /**< Knob fill color. */
    UIColor borderColor;       /**< Border color around the pill. */
    float   borderWidth;       /**< Border thickness (pixels). */
    float   knobMargin;        /**< Gap between knob edge and pill edge. */
    int     animMs;            /**< Knob slide duration in ms (0 = instant). */
    float   _phase;            /**< Internal eased phase: 0 = off position, 1 = on position. */

    UISwitchCallback onChange; /**< Fires when `on` flips. */
    void* userdata;            /**< Opaque pointer forwarded to onChange. */
};

UISwitch* UISwitch_Create     (int initialOn);
UISwitch* UISwitch_SetOn      (UISwitch* sw, int on);
int       UISwitch_IsOn       (const UISwitch* sw);
UISwitch* UISwitch_SetColors  (UISwitch* sw, UIColor off, UIColor on, UIColor knob);
UISwitch* UISwitch_SetOffColor (UISwitch* sw, UIColor color);
UISwitch* UISwitch_SetOnColor  (UISwitch* sw, UIColor color);
UISwitch* UISwitch_SetKnobColor(UISwitch* sw, UIColor color);
UISwitch* UISwitch_SetBorder   (UISwitch* sw, UIColor color, float width);
UISwitch* UISwitch_SetAnimMs   (UISwitch* sw, int ms);
UISwitch* UISwitch_OnChange    (UISwitch* sw, UISwitchCallback cb, void* userdata);
UISwitch* UISwitch_SetCursor   (UISwitch* sw, UICursor cursor);
void      UISwitch_Destroy     (UISwitch* sw);

// ---------------------------------------------------------------------
// UIRadioButton - circular toggle that participates in a mutually
// exclusive group. Pick any non-NULL `group` pointer (e.g. the address
// of a local int / config struct) and pass it to every radio in the
// same group; selecting one automatically deselects the others.
// ---------------------------------------------------------------------
typedef struct UIRadioButton UIRadioButton;
typedef void (*UIRadioCallback)(UIRadioButton* r, int selected, void* userdata);

/**
 * Mutually-exclusive radio button. Radios that share the same `group`
 * pointer behave as a single choice: selecting one automatically
 * deselects the others.
 */
struct UIRadioButton {
    const char* __widget_type; /**< Widget type tag (== UI_WIDGET_RADIO). */
    void* group;               /**< Shared pointer identifying the radio group (siblings share it). */
    int   selected;            /**< 1 when this button is the active choice of its group. */
    int   hovered;             /**< Internal: mouse currently over the button. */
    int   pressed;             /**< Internal: mouse-down without release. */
    UICursor cursor;           /**< Cursor shown on hover. */

    UIColor boxColor;          /**< Outer disc background. */
    UIColor dotColor;          /**< Inner dot color (drawn when selected). */
    UIColor borderColor;       /**< Border color. */
    float   borderWidth;       /**< Border thickness (pixels). */
    float   dotScale;          /**< Inner dot size as a fraction of the outer radius (0..1). */

    int   animMs;              /**< Toggle animation duration in ms (0 = instant). */
    float _phase;              /**< Internal eased phase: 0 = unselected, 1 = selected. */

    UIRadioCallback onChange;  /**< Fires when this button becomes selected. */
    void* userdata;            /**< Opaque pointer forwarded to onChange. */
};

UIRadioButton* UIRadio_Create        (void* group, int initialSelected);
UIRadioButton* UIRadio_SetSelected   (UIRadioButton* r, int selected);
int            UIRadio_IsSelected    (const UIRadioButton* r);
UIRadioButton* UIRadio_SetColors     (UIRadioButton* r, UIColor box, UIColor dot);
UIRadioButton* UIRadio_SetBoxColor   (UIRadioButton* r, UIColor color);
UIRadioButton* UIRadio_SetDotColor   (UIRadioButton* r, UIColor color);
UIRadioButton* UIRadio_SetBorder     (UIRadioButton* r, UIColor color, float width);
UIRadioButton* UIRadio_SetDotScale   (UIRadioButton* r, float scale);
UIRadioButton* UIRadio_SetAnimMs     (UIRadioButton* r, int ms);
UIRadioButton* UIRadio_OnChange      (UIRadioButton* r, UIRadioCallback cb, void* userdata);
UIRadioButton* UIRadio_SetCursor     (UIRadioButton* r, UICursor cursor);
void           UIRadio_Destroy       (UIRadioButton* r);

// ---------------------------------------------------------------------
// Mouse dispatchers (called by app.c). These walk the children list,
// translate mouse events into per-widget state changes and trigger
// the callbacks. Sliders, checkboxes, switches and radios share them.
// ---------------------------------------------------------------------
void UIControls_DispatchMouseMotion(UIChildren* children, float x, float y);
void UIControls_DispatchMouseDown  (UIChildren* children, float x, float y, int button);
void UIControls_DispatchMouseUp    (UIChildren* children, float x, float y, int button);

#endif // UIKIT_CONTROLS_H

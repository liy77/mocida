#include <uikit/controls.h>
#include <SDL3/SDL.h>          // for SDL_BUTTON_LEFT
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------
// UICheckbox
// ---------------------------------------------------------------------

UICheckbox* UICheckbox_Create(int initialChecked) {
    UICheckbox* c = (UICheckbox*)calloc(1, sizeof(UICheckbox));
    if (!c) return NULL;
    c->__widget_type = UI_WIDGET_CHECKBOX;
    c->checked     = initialChecked ? 1 : 0;
    c->radius      = 4.0f;
    c->boxColor    = (UIColor){ 241, 245, 249, 1.0f };
    c->checkColor  = (UIColor){ 34, 197, 94, 1.0f };
    c->borderColor = (UIColor){ 148, 163, 184, 1.0f };
    c->borderWidth = 1.0f;
    c->animMs      = 160;
    c->_phase      = c->checked ? 1.0f : 0.0f;
    c->cursor      = UI_CURSOR_POINTER;
    return c;
}

UICheckbox* UICheckbox_SetCursor(UICheckbox* c, UICursor cursor) {
    if (c) c->cursor = cursor;
    return c;
}

UICheckbox* UICheckbox_SetChecked(UICheckbox* c, int checked) {
    if (!c) return c;
    c->checked = checked ? 1 : 0;
    if (c->animMs <= 0) c->_phase = c->checked ? 1.0f : 0.0f;
    return c;
}

UICheckbox* UICheckbox_SetAnimMs(UICheckbox* c, int ms) {
    if (!c) return c;
    c->animMs = (ms < 0) ? 0 : ms;
    return c;
}

UICheckbox* UICheckbox_SetColors(UICheckbox* c, UIColor box, UIColor check) {
    if (!c) return c;
    c->boxColor   = box;
    c->checkColor = check;
    return c;
}

UICheckbox* UICheckbox_SetBoxColor(UICheckbox* c, UIColor color) {
    if (!c) return c;
    c->boxColor = color;
    return c;
}

UICheckbox* UICheckbox_SetCheckColor(UICheckbox* c, UIColor color) {
    if (!c) return c;
    c->checkColor = color;
    return c;
}

UICheckbox* UICheckbox_SetBorder(UICheckbox* c, UIColor color, float width) {
    if (!c) return c;
    c->borderColor = color;
    c->borderWidth = (width < 0.0f) ? 0.0f : width;
    return c;
}

UICheckbox* UICheckbox_SetRadius(UICheckbox* c, float radius) {
    if (!c) return c;
    c->radius = (radius < 0.0f) ? 0.0f : radius;
    return c;
}

UICheckbox* UICheckbox_OnChange(UICheckbox* c, UICheckboxCallback cb, void* userdata) {
    if (!c) return c;
    c->onChange = cb;
    c->userdata = userdata;
    return c;
}

void UICheckbox_Destroy(UICheckbox* c) {
    if (!c) return;
    free(c);
}

// ---------------------------------------------------------------------
// UISlider
// ---------------------------------------------------------------------

UISlider* UISlider_Create(float minV, float maxV, float initialValue) {
    if (maxV < minV) { float t = maxV; maxV = minV; minV = t; }
    if (initialValue < minV) initialValue = minV;
    if (initialValue > maxV) initialValue = maxV;

    UISlider* s = (UISlider*)calloc(1, sizeof(UISlider));
    if (!s) return NULL;
    s->__widget_type = UI_WIDGET_SLIDER;
    s->minValue   = minV;
    s->maxValue   = maxV;
    s->value      = initialValue;
    s->trackColor = (UIColor){ 226, 232, 240, 1.0f };
    s->fillColor  = (UIColor){ 59, 130, 246, 1.0f };
    s->knobColor  = (UIColor){ 255, 255, 255, 1.0f };
    s->trackHeight= 6.0f;
    s->knobRadius = 10.0f;
    s->cursor     = UI_CURSOR_POINTER;
    return s;
}

UISlider* UISlider_SetCursor(UISlider* s, UICursor cursor) {
    if (s) s->cursor = cursor;
    return s;
}

UISlider* UISlider_SetValue(UISlider* s, float value) {
    if (!s) return s;
    if (value < s->minValue) value = s->minValue;
    if (value > s->maxValue) value = s->maxValue;
    s->value = value;
    return s;
}

UISlider* UISlider_SetRange(UISlider* s, float minV, float maxV) {
    if (!s) return s;
    if (maxV < minV) { float t = maxV; maxV = minV; minV = t; }
    s->minValue = minV;
    s->maxValue = maxV;
    if (s->value < minV) s->value = minV;
    if (s->value > maxV) s->value = maxV;
    return s;
}

UISlider* UISlider_SetColors(UISlider* s, UIColor track, UIColor fill, UIColor knob) {
    if (!s) return s;
    s->trackColor = track;
    s->fillColor  = fill;
    s->knobColor  = knob;
    return s;
}

UISlider* UISlider_SetTrackColor(UISlider* s, UIColor color) {
    if (!s) return s;
    s->trackColor = color;
    return s;
}

UISlider* UISlider_SetFillColor(UISlider* s, UIColor color) {
    if (!s) return s;
    s->fillColor = color;
    return s;
}

UISlider* UISlider_SetKnobColor(UISlider* s, UIColor color) {
    if (!s) return s;
    s->knobColor = color;
    return s;
}

UISlider* UISlider_SetTrackHeight(UISlider* s, float height) {
    if (!s) return s;
    if (height < 0.0f) height = 0.0f;
    s->trackHeight = height;
    return s;
}

UISlider* UISlider_SetKnobRadius(UISlider* s, float radius) {
    if (!s) return s;
    if (radius < 0.0f) radius = 0.0f;
    s->knobRadius = radius;
    return s;
}

UISlider* UISlider_OnChange(UISlider* s, UISliderCallback cb, void* userdata) {
    if (!s) return s;
    s->onChange = cb;
    s->userdata = userdata;
    return s;
}

void UISlider_Destroy(UISlider* s) {
    if (!s) return;
    free(s);
}

// ---------------------------------------------------------------------
// UIProgressBar
// ---------------------------------------------------------------------

UIProgressBar* UIProgressBar_Create(float initialValue) {
    if (initialValue < 0.0f) initialValue = 0.0f;
    if (initialValue > 1.0f) initialValue = 1.0f;
    UIProgressBar* p = (UIProgressBar*)calloc(1, sizeof(UIProgressBar));
    if (!p) return NULL;
    p->__widget_type = UI_WIDGET_PROGRESS_BAR;
    p->value      = initialValue;
    p->radius     = 4.0f;
    p->trackColor = (UIColor){ 226, 232, 240, 1.0f };
    p->fillColor  = (UIColor){ 59, 130, 246, 1.0f };
    return p;
}

UIProgressBar* UIProgressBar_SetValue(UIProgressBar* p, float value) {
    if (!p) return p;
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    p->value = value;
    return p;
}

UIProgressBar* UIProgressBar_SetIndeterminate(UIProgressBar* p, int yes) {
    if (!p) return p;
    p->indeterminate = yes ? 1 : 0;
    return p;
}

UIProgressBar* UIProgressBar_SetColors(UIProgressBar* p, UIColor track, UIColor fill) {
    if (!p) return p;
    p->trackColor = track;
    p->fillColor  = fill;
    return p;
}

UIProgressBar* UIProgressBar_SetTrackColor(UIProgressBar* p, UIColor color) {
    if (!p) return p;
    p->trackColor = color;
    return p;
}

UIProgressBar* UIProgressBar_SetFillColor(UIProgressBar* p, UIColor color) {
    if (!p) return p;
    p->fillColor = color;
    return p;
}

UIProgressBar* UIProgressBar_SetRadius(UIProgressBar* p, float radius) {
    if (!p) return p;
    if (radius < 0.0f) radius = 0.0f;
    p->radius = radius;
    return p;
}

void UIProgressBar_Destroy(UIProgressBar* p) {
    if (!p) return;
    free(p);
}

// ---------------------------------------------------------------------
// UISpinner
// ---------------------------------------------------------------------

UISpinner* UISpinner_Create(float radius) {
    if (radius <= 0.0f) radius = 16.0f;
    UISpinner* s = (UISpinner*)calloc(1, sizeof(UISpinner));
    if (!s) return NULL;
    s->__widget_type = UI_WIDGET_SPINNER;
    s->radius    = radius;
    s->thickness = 3.0f;
    s->speed     = 6.28318530718f; // ~ 2*pi rad/s (1 revolution per second)
    s->color     = (UIColor){ 59, 130, 246, 1.0f };
    return s;
}

UISpinner* UISpinner_SetColor(UISpinner* s, UIColor color) {
    if (!s) return s;
    s->color = color;
    return s;
}

UISpinner* UISpinner_SetThickness(UISpinner* s, float thickness) {
    if (!s) return s;
    if (thickness < 0.0f) thickness = 0.0f;
    s->thickness = thickness;
    return s;
}

UISpinner* UISpinner_SetRadius(UISpinner* s, float radius) {
    if (!s) return s;
    if (radius < 1.0f) radius = 1.0f;
    s->radius = radius;
    return s;
}

UISpinner* UISpinner_SetSpeed(UISpinner* s, float speedPerSec) {
    if (!s) return s;
    s->speed = speedPerSec;
    return s;
}

void UISpinner_Destroy(UISpinner* s) {
    if (!s) return;
    free(s);
}

// ---------------------------------------------------------------------
// UISwitch
// ---------------------------------------------------------------------

UISwitch* UISwitch_Create(int initialOn) {
    UISwitch* sw = (UISwitch*)calloc(1, sizeof(UISwitch));
    if (!sw) return NULL;
    sw->__widget_type = UI_WIDGET_SWITCH;
    sw->on            = initialOn ? 1 : 0;
    sw->_phase        = sw->on ? 1.0f : 0.0f;
    sw->offColor      = (UIColor){ 203, 213, 225, 1.0f };
    sw->onColor       = (UIColor){ 34, 197, 94, 1.0f };
    sw->knobColor     = (UIColor){ 255, 255, 255, 1.0f };
    sw->borderColor   = (UIColor){ 0, 0, 0, 0.0f };
    sw->borderWidth   = 0.0f;
    sw->knobMargin    = 3.0f;
    sw->animMs        = 140;
    sw->cursor        = UI_CURSOR_POINTER;
    return sw;
}

UISwitch* UISwitch_SetCursor(UISwitch* sw, UICursor cursor) {
    if (sw) sw->cursor = cursor;
    return sw;
}

UISwitch* UISwitch_SetOn(UISwitch* sw, int on) {
    if (!sw) return sw;
    sw->on = on ? 1 : 0;
    // Snap immediately when animation is disabled.
    if (sw->animMs <= 0) sw->_phase = sw->on ? 1.0f : 0.0f;
    return sw;
}

int UISwitch_IsOn(const UISwitch* sw) {
    return sw ? sw->on : 0;
}

UISwitch* UISwitch_SetColors(UISwitch* sw, UIColor off, UIColor on, UIColor knob) {
    if (!sw) return sw;
    sw->offColor  = off;
    sw->onColor   = on;
    sw->knobColor = knob;
    return sw;
}

UISwitch* UISwitch_SetOffColor(UISwitch* sw, UIColor color) {
    if (!sw) return sw;
    sw->offColor = color;
    return sw;
}

UISwitch* UISwitch_SetOnColor(UISwitch* sw, UIColor color) {
    if (!sw) return sw;
    sw->onColor = color;
    return sw;
}

UISwitch* UISwitch_SetKnobColor(UISwitch* sw, UIColor color) {
    if (!sw) return sw;
    sw->knobColor = color;
    return sw;
}

UISwitch* UISwitch_SetBorder(UISwitch* sw, UIColor color, float width) {
    if (!sw) return sw;
    sw->borderColor = color;
    sw->borderWidth = (width < 0.0f) ? 0.0f : width;
    return sw;
}

UISwitch* UISwitch_SetAnimMs(UISwitch* sw, int ms) {
    if (!sw) return sw;
    sw->animMs = (ms < 0) ? 0 : ms;
    return sw;
}

UISwitch* UISwitch_OnChange(UISwitch* sw, UISwitchCallback cb, void* userdata) {
    if (!sw) return sw;
    sw->onChange = cb;
    sw->userdata = userdata;
    return sw;
}

void UISwitch_Destroy(UISwitch* sw) {
    if (!sw) return;
    free(sw);
}

// ---------------------------------------------------------------------
// UIRadioButton
// ---------------------------------------------------------------------

UIRadioButton* UIRadio_Create(void* group, int initialSelected) {
    UIRadioButton* r = (UIRadioButton*)calloc(1, sizeof(UIRadioButton));
    if (!r) return NULL;
    r->__widget_type = UI_WIDGET_RADIO;
    r->group         = group;
    r->selected      = initialSelected ? 1 : 0;
    r->_phase        = r->selected ? 1.0f : 0.0f;
    r->boxColor      = (UIColor){ 255, 255, 255, 1.0f };
    r->dotColor      = (UIColor){ 59, 130, 246, 1.0f };
    r->borderColor   = (UIColor){ 148, 163, 184, 1.0f };
    r->borderWidth   = 1.5f;
    r->dotScale      = 0.50f;
    r->animMs        = 160;
    r->cursor        = UI_CURSOR_POINTER;
    return r;
}

UIRadioButton* UIRadio_SetCursor(UIRadioButton* r, UICursor cursor) {
    if (r) r->cursor = cursor;
    return r;
}

UIRadioButton* UIRadio_SetSelected(UIRadioButton* r, int selected) {
    if (!r) return r;
    r->selected = selected ? 1 : 0;
    if (r->animMs <= 0) r->_phase = r->selected ? 1.0f : 0.0f;
    return r;
}

int UIRadio_IsSelected(const UIRadioButton* r) {
    return r ? r->selected : 0;
}

UIRadioButton* UIRadio_SetColors(UIRadioButton* r, UIColor box, UIColor dot) {
    if (!r) return r;
    r->boxColor = box;
    r->dotColor = dot;
    return r;
}

UIRadioButton* UIRadio_SetBoxColor(UIRadioButton* r, UIColor color) {
    if (!r) return r;
    r->boxColor = color;
    return r;
}

UIRadioButton* UIRadio_SetDotColor(UIRadioButton* r, UIColor color) {
    if (!r) return r;
    r->dotColor = color;
    return r;
}

UIRadioButton* UIRadio_SetBorder(UIRadioButton* r, UIColor color, float width) {
    if (!r) return r;
    r->borderColor = color;
    r->borderWidth = (width < 0.0f) ? 0.0f : width;
    return r;
}

UIRadioButton* UIRadio_SetDotScale(UIRadioButton* r, float scale) {
    if (!r) return r;
    if (scale < 0.0f) scale = 0.0f;
    if (scale > 1.0f) scale = 1.0f;
    r->dotScale = scale;
    return r;
}

UIRadioButton* UIRadio_SetAnimMs(UIRadioButton* r, int ms) {
    if (!r) return r;
    r->animMs = (ms < 0) ? 0 : ms;
    return r;
}

UIRadioButton* UIRadio_OnChange(UIRadioButton* r, UIRadioCallback cb, void* userdata) {
    if (!r) return r;
    r->onChange = cb;
    r->userdata = userdata;
    return r;
}

void UIRadio_Destroy(UIRadioButton* r) {
    if (!r) return;
    free(r);
}

// ---------------------------------------------------------------------
// Mouse dispatch (shared across Checkbox + Slider). Iterates children,
// hit-tests each control widget and updates its internal state.
// ---------------------------------------------------------------------

static int InsideRect(float x, float y, float rx, float ry, float rw, float rh) {
    return (x >= rx && x < rx + rw && y >= ry && y < ry + rh);
}

void UIControls_DispatchMouseMotion(UIChildren* children, float x, float y) {
    if (!children) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        if (!w || !w->visible || !w->data || !w->width || !w->height) continue;
        UIWidgetBase* base = (UIWidgetBase*)w->data;

        // Checkbox: just hover/press feedback.
        if (!strcmp(base->__widget_type, UI_WIDGET_CHECKBOX)) {
            UICheckbox* c = (UICheckbox*)base;
            c->hovered = InsideRect(x, y, w->x, w->y, *w->width, *w->height);
            continue;
        }

        // Switch: same hover feedback as Checkbox.
        if (!strcmp(base->__widget_type, UI_WIDGET_SWITCH)) {
            UISwitch* sw = (UISwitch*)base;
            sw->hovered = InsideRect(x, y, w->x, w->y, *w->width, *w->height);
            continue;
        }

        // Radio: same hover feedback.
        if (!strcmp(base->__widget_type, UI_WIDGET_RADIO)) {
            UIRadioButton* r = (UIRadioButton*)base;
            r->hovered = InsideRect(x, y, w->x, w->y, *w->width, *w->height);
            continue;
        }

        // Slider: while dragging the knob, map the cursor x to a value
        // along the track. Hit area is the full widget bounds (the
        // track sits inside it but we don't restrict to the track row -
        // matches what users expect from a scrubber).
        if (!strcmp(base->__widget_type, UI_WIDGET_SLIDER)) {
            UISlider* s = (UISlider*)base;
            if (s->dragging) {
                const float ww = *w->width;
                const float ratio = (ww > 0.0f) ? ((x - w->x) / ww) : 0.0f;
                float clamped = ratio;
                if (clamped < 0.0f) clamped = 0.0f;
                if (clamped > 1.0f) clamped = 1.0f;
                const float v = s->minValue + (s->maxValue - s->minValue) * clamped;
                if (v != s->value) {
                    s->value = v;
                    if (s->onChange) s->onChange(s, v, s->userdata);
                }
            }
        }
    }
}

void UIControls_DispatchMouseDown(UIChildren* children, float x, float y, int button) {
    if (!children || button != SDL_BUTTON_LEFT) return;
    for (int i = children->count - 1; i >= 0; i--) {
        UIWidget* w = children->children[i];
        if (!w || !w->visible || !w->data || !w->width || !w->height) continue;
        if (!InsideRect(x, y, w->x, w->y, *w->width, *w->height)) continue;
        UIWidgetBase* base = (UIWidgetBase*)w->data;

        if (!strcmp(base->__widget_type, UI_WIDGET_CHECKBOX)) {
            UICheckbox* c = (UICheckbox*)base;
            c->pressed = 1;
            return;
        }
        if (!strcmp(base->__widget_type, UI_WIDGET_SWITCH)) {
            UISwitch* sw = (UISwitch*)base;
            sw->pressed = 1;
            return;
        }
        if (!strcmp(base->__widget_type, UI_WIDGET_RADIO)) {
            UIRadioButton* r = (UIRadioButton*)base;
            r->pressed = 1;
            return;
        }
        if (!strcmp(base->__widget_type, UI_WIDGET_SLIDER)) {
            UISlider* s = (UISlider*)base;
            s->dragging = 1;
            // Immediately scrub to the clicked position so the click
            // doesn't feel like it does nothing.
            const float ww = *w->width;
            const float ratio = (ww > 0.0f) ? ((x - w->x) / ww) : 0.0f;
            float clamped = ratio;
            if (clamped < 0.0f) clamped = 0.0f;
            if (clamped > 1.0f) clamped = 1.0f;
            const float v = s->minValue + (s->maxValue - s->minValue) * clamped;
            if (v != s->value) {
                s->value = v;
                if (s->onChange) s->onChange(s, v, s->userdata);
            }
            return;
        }
    }
}

void UIControls_DispatchMouseUp(UIChildren* children, float x, float y, int button) {
    (void)x; (void)y;
    if (!children || button != SDL_BUTTON_LEFT) return;
    for (int i = 0; i < children->count; i++) {
        UIWidget* w = children->children[i];
        if (!w || !w->data || !w->width || !w->height) continue;
        UIWidgetBase* base = (UIWidgetBase*)w->data;

        if (!strcmp(base->__widget_type, UI_WIDGET_CHECKBOX)) {
            UICheckbox* c = (UICheckbox*)base;
            if (c->pressed && c->hovered) {
                c->checked = !c->checked;
                if (c->animMs <= 0) c->_phase = c->checked ? 1.0f : 0.0f;
                if (c->onChange) c->onChange(c, c->checked, c->userdata);
            }
            c->pressed = 0;
        }
        if (!strcmp(base->__widget_type, UI_WIDGET_SWITCH)) {
            UISwitch* sw = (UISwitch*)base;
            if (sw->pressed && sw->hovered) {
                sw->on = !sw->on;
                if (sw->animMs <= 0) sw->_phase = sw->on ? 1.0f : 0.0f;
                if (sw->onChange) sw->onChange(sw, sw->on, sw->userdata);
            }
            sw->pressed = 0;
        }
        if (!strcmp(base->__widget_type, UI_WIDGET_RADIO)) {
            UIRadioButton* r = (UIRadioButton*)base;
            if (r->pressed && r->hovered && !r->selected) {
                r->selected = 1;
                if (r->animMs <= 0) r->_phase = 1.0f;
                // Deselect siblings in the same group.
                for (int j = 0; j < children->count; j++) {
                    UIWidget* sib = children->children[j];
                    if (!sib || sib == w || !sib->data) continue;
                    UIWidgetBase* sb = (UIWidgetBase*)sib->data;
                    if (strcmp(sb->__widget_type, UI_WIDGET_RADIO) != 0) continue;
                    UIRadioButton* other = (UIRadioButton*)sb;
                    if (other->group != r->group || !other->selected) continue;
                    other->selected = 0;
                    if (other->animMs <= 0) other->_phase = 0.0f;
                    if (other->onChange) {
                        other->onChange(other, 0, other->userdata);
                    }
                }
                if (r->onChange) r->onChange(r, 1, r->userdata);
            }
            r->pressed = 0;
        }
        if (!strcmp(base->__widget_type, UI_WIDGET_SLIDER)) {
            UISlider* s = (UISlider*)base;
            s->dragging = 0;
        }
    }
}

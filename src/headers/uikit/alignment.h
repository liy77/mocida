#ifndef UIKIT_ALIGNMENTS_H
#define UIKIT_ALIGNMENTS_H

#include <stdint.h>

typedef uint8_t UIAlign;

#define UI_ALIGN_V_CENTER (UIAlign)0x00000001
#define UI_ALIGN_V_TOP (UIAlign)0x00000002
#define UI_ALIGN_V_BOTTOM (UIAlign)0x00000004
#define UI_ALIGN_H_CENTER (UIAlign)0x00000008
#define UI_ALIGN_H_LEFT (UIAlign)0x00000010
#define UI_ALIGN_H_RIGHT (UIAlign)0x00000020

/**
 * UIAlignment structure representing the alignment of a widget within its parent.
 * It contains properties for vertical and horizontal alignment.
 */
typedef struct {
    UIAlign vertical;
    UIAlign horizontal;
} UIAlignment;

/**
 * Creates a UIAlignment object with the specified vertical and horizontal alignment.
 * @param vertical Vertical alignment (e.g., UI_ALIGN_V_CENTER).
 * @param horizontal Horizontal alignment (e.g., UI_ALIGN_H_CENTER).
 * @return A UIAlignment object.
 */
UIAlignment UIAlignment_Create(UIAlign vertical, UIAlign horizontal);

#endif // UIKIT_ALIGNMENTS_H
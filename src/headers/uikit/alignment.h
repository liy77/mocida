#ifndef UIKIT_ALIGNMENTS_H
#define UIKIT_ALIGNMENTS_H

#include <uikit/widget.h>
#include <uikit/rect.h>
#include <uikit/text.h>

#define UI_ALIGN_V_CENTER 0x00000001
#define UI_ALIGN_V_TOP 0x00000002
#define UI_ALIGN_V_BOTTOM 0x00000004
#define UI_ALIGN_H_CENTER 0x00000008
#define UI_ALIGN_H_LEFT 0x00000010
#define UI_ALIGN_H_RIGHT 0x00000020

typedef struct {
    int vertical;
    int horizontal;
} UIAlignment;

UIAlignment UIAlignment_Create(int vertical, int horizontal);

UIAlignment UIAlignment_VerticalCenter();

UIAlignment UIAlignment_VerticalTop();

UIAlignment UIAlignment_VerticalBottom();

UIAlignment UIAlignment_HorizontalCenter();

UIAlignment UIAlignment_HorizontalLeft();

UIAlignment UIAlignment_HorizontalRight();

void UIAlignment_SetWidgetAlignment(UIWidget* widget, UIAlignment alignment);

#endif // UIKIT_ALIGNMENTS_H
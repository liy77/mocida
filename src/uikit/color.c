#include <uikit/color.h>
#include <stdio.h>

UIColor* UIColor_RGBA(int r, int g, int b, float a) {
    UIColor* color = (UIColor*)malloc(sizeof(UIColor));
    if (color == NULL) {
        fprintf(stderr, "Failed to allocate memory for UIColor\n");
        return NULL;
    }

    // Clamp values to valid ranges
    if (r < 0) {
        r = 0;
    } else if (r > 255) {
        r = 255;
    }

    if (g < 0) {
        g = 0;
    } else if (g > 255) {
        g = 255;
    }

    if (b < 0) {
        b = 0;
    } else if (b > 255) {
        b = 255;
    }

    if (a < 0.0f) {
        a = 0.0f;
    } else if (a > 1.0f) {
        a = 1.0f;
    }

    color->r = r;
    color->g = g;
    color->b = b;
    color->a = a;

    return color;
}

UIColor* UIColor_Hex(const char* hex) {
    int r, g, b;
    if (sscanf_s(hex, "#%02x%02x%02x", &r, &g, &b) == 3) {
        return UIColor_RGBA(r, g, b, 1.0f);
    }
    return NULL; // Invalid hex string
}
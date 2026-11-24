// sfsymbol_cocoa.mm — rasterise Apple SF Symbols into SDL textures/surfaces.
//
// Image sources of the form `sf://play.fill` (also `symbol://play.fill`) are
// resolved here on Apple platforms via AppKit / UIKit system symbols, so MUI
// can use real SF Symbols instead of hand-drawn PNGs:
//
//   Image("sf://play.fill", width: 18, height: 18, tint: #FFFFFF)
//
// Non-Apple builds link sfsymbol_stub.c (always returns NULL).

#import <TargetConditionals.h>
#include <uikit/asset.h>
#include <uikit/debug.h>
#include <SDL3/SDL.h>
#include <string.h>
#include <math.h>

#if defined(__APPLE__) && TARGET_OS_OSX
#import <AppKit/AppKit.h>
#elif defined(__APPLE__) && TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#endif

static int IsSFSymbolURI(const char* path, const char** outName) {
    if (!path) return 0;
    if (SDL_strncasecmp(path, "sf://", 5) == 0) {
        if (outName) *outName = path + 5;
        return 1;
    }
    if (SDL_strncasecmp(path, "symbol://", 9) == 0) {
        if (outName) *outName = path + 9;
        return 1;
    }
    return 0;
}

#if defined(__APPLE__) && TARGET_OS_OSX

static SDL_Surface* RasterizeSFSymbolMac(const char* name, int pixels) {
    if (!name || !*name) return NULL;
    const int edge = pixels > 0 ? pixels : 64;
    @autoreleasepool {
        NSString* nsName = [NSString stringWithUTF8String:name];
        if (!nsName.length) return NULL;

        NSImage* img = [NSImage imageWithSystemSymbolName:nsName
                                 accessibilityDescription:nil];
        if (!img) {
            UI_WARN(UI_CAT_ASSET, "SF Symbol '%s' not found", name);
            return NULL;
        }

        // Render at 2× the widget edge so Metal/HiDPI stays sharp; the
        // Image widget then scales into its logical box with linear filter.
        const CGFloat pointSize = (CGFloat)edge * 0.85;
        NSImageSymbolConfiguration* cfg =
            [NSImageSymbolConfiguration configurationWithPointSize:pointSize
                                                            weight:NSFontWeightRegular
                                                             scale:NSImageSymbolScaleMedium];
        NSImage* configured = [img imageWithSymbolConfiguration:cfg] ?: img;

        NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:NULL
                          pixelsWide:edge
                          pixelsHigh:edge
                       bitsPerSample:8
                     samplesPerPixel:4
                            hasAlpha:YES
                            isPlanar:NO
                      colorSpaceName:NSDeviceRGBColorSpace
                         bytesPerRow:0
                        bitsPerPixel:0];
        if (!rep) return NULL;

        [NSGraphicsContext saveGraphicsState];
        NSGraphicsContext* ctx = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
        [NSGraphicsContext setCurrentContext:ctx];
        [[NSColor clearColor] set];
        NSRectFill(NSMakeRect(0, 0, edge, edge));
        NSRect dst = NSMakeRect(0, 0, edge, edge);
        [configured drawInRect:dst
                      fromRect:NSZeroRect
                     operation:NSCompositingOperationSourceOver
                      fraction:1.0
                respectFlipped:YES
                         hints:@{ NSImageHintInterpolation: @(NSImageInterpolationHigh) }];
        [NSGraphicsContext restoreGraphicsState];

        const int w = (int)rep.pixelsWide;
        const int h = (int)rep.pixelsHigh;
        const int pitch = (int)rep.bytesPerRow;
        unsigned char* src = [rep bitmapData];
        if (!src || w <= 0 || h <= 0) return NULL;

        SDL_Surface* surf = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
        if (!surf) return NULL;
        // AppKit template glyphs are black+alpha. Force RGB=white so SDL
        // ColorMod tint (multiply) can recolor them.
        for (int y = 0; y < h; y++) {
            unsigned char* dstRow = (unsigned char*)surf->pixels + y * surf->pitch;
            unsigned char* srcRow = src + y * pitch;
            for (int x = 0; x < w; x++) {
                unsigned char a = srcRow[x * 4 + 3];
                dstRow[x * 4 + 0] = 255;
                dstRow[x * 4 + 1] = 255;
                dstRow[x * 4 + 2] = 255;
                dstRow[x * 4 + 3] = a;
            }
        }
        return surf;
    }
}

#elif defined(__APPLE__) && TARGET_OS_IPHONE

static SDL_Surface* RasterizeSFSymbolIOS(const char* name, int pixels) {
    if (!name || !*name) return NULL;
    const int edge = pixels > 0 ? pixels : 64;
    @autoreleasepool {
        NSString* nsName = [NSString stringWithUTF8String:name];
        UIImage* img = [UIImage systemImageNamed:nsName];
        if (!img) {
            UI_WARN(UI_CAT_ASSET, "SF Symbol '%s' not found", name);
            return NULL;
        }
        UIImageSymbolConfiguration* cfg =
            [UIImageSymbolConfiguration configurationWithPointSize:(CGFloat)edge * 0.85
                                                            weight:UIImageSymbolWeightRegular
                                                             scale:UIImageSymbolScaleMedium];
        img = [img imageByApplyingSymbolConfiguration:cfg] ?: img;
        img = [img imageWithTintColor:[UIColor whiteColor]
                        renderingMode:UIImageRenderingModeAlwaysOriginal];

        CGSize size = CGSizeMake(edge, edge);
        UIGraphicsBeginImageContextWithOptions(size, NO, 1.0);
        [img drawInRect:CGRectMake(0, 0, edge, edge)];
        UIImage* drawn = UIGraphicsGetImageFromCurrentImageContext();
        UIGraphicsEndImageContext();
        if (!drawn) return NULL;

        CGImageRef cg = drawn.CGImage;
        if (!cg) return NULL;
        const size_t w = CGImageGetWidth(cg);
        const size_t h = CGImageGetHeight(cg);
        SDL_Surface* surf = SDL_CreateSurface((int)w, (int)h, SDL_PIXELFORMAT_RGBA32);
        if (!surf) return NULL;
        CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
        CGContextRef ctx = CGBitmapContextCreate(
            surf->pixels, w, h, 8, (size_t)surf->pitch, space,
            kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
        CGColorSpaceRelease(space);
        if (!ctx) {
            SDL_DestroySurface(surf);
            return NULL;
        }
        CGContextClearRect(ctx, CGRectMake(0, 0, w, h));
        CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), cg);
        CGContextRelease(ctx);
        return surf;
    }
}

#endif

extern "C" SDL_Surface* UIAsset_LoadSFSymbolSurface(const char* uri, int pixels) {
    const char* name = NULL;
    if (!IsSFSymbolURI(uri, &name) || !name || !*name) return NULL;
#if defined(__APPLE__) && TARGET_OS_OSX
    return RasterizeSFSymbolMac(name, pixels);
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    return RasterizeSFSymbolIOS(name, pixels);
#else
    (void)pixels;
    return NULL;
#endif
}

extern "C" SDL_Texture* UIAsset_LoadSFSymbolTexture(SDL_Renderer* renderer,
                                                    const char* uri,
                                                    int pixels) {
    if (!renderer) return NULL;
    SDL_Surface* surf = UIAsset_LoadSFSymbolSurface(uri, pixels);
    if (!surf) return NULL;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_DestroySurface(surf);
    if (tex) SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    return tex;
}

extern "C" int UIAsset_IsSFSymbolURI(const char* path) {
    return IsSFSymbolURI(path, NULL);
}

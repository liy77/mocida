#include <uikit/upscale.h>
#include <SDL3/SDL.h>

#if defined(__APPLE__)
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>
#import <QuartzCore/CAMetalLayer.h>
#endif

int UIUpscale_MetalFXSupported(void) {
#if !defined(__APPLE__)
    return 0;
#else
    if (@available(macOS 13.0, iOS 16.0, *)) {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) return 0;
        return [MTLFXSpatialScalerDescriptor supportsDevice:device] ? 1 : 0;
    }
    return 0;
#endif
}

void UIUpscale_ApplyFrame(SDL_Renderer* renderer) {
#if !defined(__APPLE__)
    (void)renderer;
    return;
#else
    if (UIUpscale_Mode() != UI_UPSCALE_METALFX || !renderer) return;
    if (@available(macOS 13.0, iOS 16.0, *)) {
        void* layer_ptr = SDL_GetRenderMetalLayer(renderer);
        if (!layer_ptr) return;
        CAMetalLayer* layer = (__bridge CAMetalLayer*)layer_ptr;
        id<MTLDevice> device = layer.device;
        if (!device || ![MTLFXSpatialScalerDescriptor supportsDevice:device]) return;

        int rw = 0, rh = 0;
        UIUpscale_RenderSize(&rw, &rh);
        const int dw = (int)layer.drawableSize.width;
        const int dh = (int)layer.drawableSize.height;
        if (rw < 2 || rh < 2 || dw < 2 || dh < 2 || (rw == dw && rh == dh)) return;

        static id<MTLFXSpatialScaler> scaler = nil;
        static NSUInteger inW = 0, inH = 0, outW = 0, outH = 0;
        if (!scaler || inW != (NSUInteger)rw || inH != (NSUInteger)rh || outW != (NSUInteger)dw || outH != (NSUInteger)dh) {
            MTLFXSpatialScalerDescriptor* desc = [MTLFXSpatialScalerDescriptor new];
            desc.inputWidth = (NSUInteger)rw;
            desc.inputHeight = (NSUInteger)rh;
            desc.outputWidth = (NSUInteger)dw;
            desc.outputHeight = (NSUInteger)dh;
            desc.colorTextureFormat = MTLPixelFormatBGRA8Unorm;
            desc.outputTextureFormat = MTLPixelFormatBGRA8Unorm;
            desc.colorProcessingMode = MTLFXSpatialScalerColorProcessingModePerceptual;
            scaler = [desc newSpatialScalerWithDevice:device];
            inW = desc.inputWidth;
            inH = desc.inputHeight;
            outW = desc.outputWidth;
            outH = desc.outputHeight;
        }
        if (!scaler) return;

        id<MTLCommandQueue> queue = [device newCommandQueue];
        id<CAMetalDrawable> drawable = [layer nextDrawable];
        if (!queue || !drawable) return;

        MTLTextureDescriptor* srcDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                            width:(NSUInteger)rw
                                                                                           height:(NSUInteger)rh
                                                                                        mipmapped:NO];
        srcDesc.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
        srcDesc.storageMode = MTLStorageModePrivate;
        id<MTLTexture> color = [device newTextureWithDescriptor:srcDesc];
        if (!color) return;

        // The game already drew this frame into SDL's drawable. Copy a
        // downscaled read of the presented layer is not available here, so
        // the scaler is configured and ready for a renderer that draws into
        // `color` at the internal size. Encoding with the current drawable
        // keeps the MetalFX pass on the same device as the window.
        scaler.colorTexture = color;
        scaler.inputContentWidth = (NSUInteger)rw;
        scaler.inputContentHeight = (NSUInteger)rh;
        scaler.outputTexture = drawable.texture;
        id<MTLCommandBuffer> buffer = [queue commandBuffer];
        [scaler encodeToCommandBuffer:buffer];
        [buffer presentDrawable:drawable];
        [buffer commit];
    }
#endif
}

#ifndef UIKIT_SCENE3D_H
#define UIKIT_SCENE3D_H

#include <SDL3/SDL.h>
#include <stdint.h>
#include <uikit/widget.h>
#include <uikit/color.h>
#include <uikit/shader.h>

/**
 * Native 2.5D / 3D-aux scene widget.
 *
 * Depth-sorted textured sprites, point lights, a simple camera, and an
 * optional Mesh3D triangle list share one API used by Mocidad games and
 * MUI. When a GPU shader library is attached the active program
 * (`hd2d`, `lit_sprite`, `unlit`, or a custom pass) drives lighting;
 * otherwise a CPU approximation keeps the scene readable on software
 * backends — same API on every platform mocida ships.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define UI_WIDGET_SCENE3D "@uikit/scene3d"

typedef enum {
    UI_GFX_MODE_2D = 0,
    UI_GFX_MODE_25D,
    UI_GFX_MODE_3D_AUX
} UIGfxMode;

typedef enum {
    UI_BILLBOARD_NONE = 0,
    UI_BILLBOARD_Y_AXIS,
    UI_BILLBOARD_SPHERICAL
} UIBillboardMode;

typedef struct {
    float x, y, z;
    float yaw, pitch;   /**< degrees */
    float zoom;         /**< ortho scale; 1 = identity */
} UICamera3D;

typedef struct UISprite3D {
    char* source;
    SDL_Texture* texture;   /**< owned when loaded from source */
    float x, y, z;
    float w, h;
    float rotation;         /**< degrees, around Z */
    float opacity;
    UIColor tint;
    UIBillboardMode billboard;
    int castsShadow;
    int visible;
} UISprite3D;

typedef struct UIPointLight {
    float x, y, z;
    float radius;
    float intensity;
    UIColor color;
    int shadows;
    int enabled;
} UIPointLight;

typedef struct UIMesh3D {
    float* positions;   /**< xyz xyz …, length = vertexCount * 3 */
    float* uvs;         /**< uv uv … or NULL */
    uint32_t* indices;  /**< optional; NULL → triangle list */
    int vertexCount;
    int indexCount;
    float x, y, z;
    float scale;
    UIColor color;
    int visible;
} UIMesh3D;

typedef struct UIScene3D UIScene3D;

UIScene3D* UIScene3D_Create(float width, float height);
void       UIScene3D_Destroy(UIScene3D* scene);

UIWidget*  UIScene3D_AsWidget(UIScene3D* scene);
UIWidget*  UIScene3D_ToWidget(UIScene3D* scene);
UIScene3D* UIScene3D_FromWidget(UIWidget* widget);

UIScene3D* UIScene3D_SetSize(UIScene3D* scene, float width, float height);
UIScene3D* UIScene3D_SetMode(UIScene3D* scene, UIGfxMode mode);
UIGfxMode  UIScene3D_GetMode(const UIScene3D* scene);

UIScene3D* UIScene3D_SetBackground(UIScene3D* scene, UIColor color);
/** Borrow `lib` (caller keeps ownership). */
UIScene3D* UIScene3D_SetShaderLibrary(UIScene3D* scene, UIShaderLibrary* lib);
/**
 * Transfer ownership of `lib` to the scene. Destroyed with the scene.
 * Pass NULL to clear.
 */
UIScene3D* UIScene3D_OwnShaderLibrary(UIScene3D* scene, UIShaderLibrary* lib);
UIScene3D* UIScene3D_UseShader(UIScene3D* scene, const char* name);

UICamera3D* UIScene3D_Camera(UIScene3D* scene);

UISprite3D* UIScene3D_AddSprite(UIScene3D* scene, const char* source,
                                float x, float y, float z,
                                float w, float h);
UISprite3D* UIScene3D_SpriteAt(UIScene3D* scene, int index);
int         UIScene3D_SpriteCount(const UIScene3D* scene);
void        UIScene3D_ClearSprites(UIScene3D* scene);

UIPointLight* UIScene3D_AddPointLight(UIScene3D* scene,
                                      float x, float y, float z,
                                      float radius, float intensity,
                                      UIColor color);
UIPointLight* UIScene3D_LightAt(UIScene3D* scene, int index);
int           UIScene3D_LightCount(const UIScene3D* scene);

UIMesh3D* UIScene3D_AddMesh(UIScene3D* scene,
                            const float* positions, int vertexCount,
                            const uint32_t* indices, int indexCount);
UIMesh3D* UIScene3D_MeshAt(UIScene3D* scene, int index);
int       UIScene3D_MeshCount(const UIScene3D* scene);

/**
 * Depth-sorts and paints into `renderer` at the widget origin. Called
 * from the window render path; advanced hosts may call it directly.
 */
void UIScene3D_Render(UIScene3D* scene, SDL_Renderer* renderer,
                      float originX, float originY, float opacity);

#ifdef __cplusplus
}
#endif

#endif /* UIKIT_SCENE3D_H */

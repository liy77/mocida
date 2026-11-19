#include <uikit/scene3d.h>
#include <uikit/asset.h>
#include <uikit/debug.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define UI_SCENE_MAX_SPRITES 512
#define UI_SCENE_MAX_LIGHTS  32
#define UI_SCENE_MAX_MESHES  64

struct UIScene3D {
    const char* __widget_type;
    float width;
    float height;
    UIGfxMode mode;
    UIColor background;
    UICamera3D camera;
    UIShaderLibrary* shaders;
    int ownsShaders; /* 1 when shaders destroyed with the scene */
    char activeShader[64];

    UISprite3D sprites[UI_SCENE_MAX_SPRITES];
    int spriteCount;
    UIPointLight lights[UI_SCENE_MAX_LIGHTS];
    int lightCount;
    UIMesh3D meshes[UI_SCENE_MAX_MESHES];
    int meshCount;
};

static char* DupStr(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char* o = (char*)malloc(n);
    if (o) memcpy(o, s, n);
    return o;
}

UIScene3D* UIScene3D_Create(float width, float height) {
    UIScene3D* s = (UIScene3D*)calloc(1, sizeof(UIScene3D));
    if (!s) return NULL;
    s->__widget_type = UI_WIDGET_SCENE3D;
    s->width = width > 0 ? width : 1.0f;
    s->height = height > 0 ? height : 1.0f;
    s->mode = UI_GFX_MODE_25D;
    s->background = (UIColor){ 0, 0, 0, 0 };
    s->camera.zoom = 1.0f;
    snprintf(s->activeShader, sizeof(s->activeShader), "hd2d");
    return s;
}

void UIScene3D_Destroy(UIScene3D* scene) {
    if (!scene) return;
    UIScene3D_ClearSprites(scene);
    for (int i = 0; i < scene->meshCount; i++) {
        free(scene->meshes[i].positions);
        free(scene->meshes[i].uvs);
        free(scene->meshes[i].indices);
    }
    if (scene->ownsShaders && scene->shaders) {
        UIShader_DestroyLibrary(scene->shaders);
        scene->shaders = NULL;
    }
    free(scene);
}

UIWidget* UIScene3D_AsWidget(UIScene3D* scene) {
    return widgc((UIWidgetData)scene);
}

UIWidget* UIScene3D_ToWidget(UIScene3D* scene) {
    if (!scene) return NULL;
    return widgcs((UIWidgetData)scene, scene->width, scene->height);
}

UIScene3D* UIScene3D_FromWidget(UIWidget* widget) {
    if (!widget || !widget->data) return NULL;
    UIWidgetBase* base = (UIWidgetBase*)widget->data;
    if (!UIWidget_TypeIs(base->__widget_type, UI_WIDGET_SCENE3D)) return NULL;
    return (UIScene3D*)widget->data;
}

UIScene3D* UIScene3D_SetSize(UIScene3D* scene, float width, float height) {
    if (!scene) return NULL;
    if (width > 0.0f) scene->width = width;
    if (height > 0.0f) scene->height = height;
    return scene;
}

UIScene3D* UIScene3D_SetMode(UIScene3D* scene, UIGfxMode mode) {
    if (scene) scene->mode = mode;
    return scene;
}

UIGfxMode UIScene3D_GetMode(const UIScene3D* scene) {
    return scene ? scene->mode : UI_GFX_MODE_2D;
}

UIScene3D* UIScene3D_SetBackground(UIScene3D* scene, UIColor color) {
    if (scene) scene->background = color;
    return scene;
}

UIScene3D* UIScene3D_SetShaderLibrary(UIScene3D* scene, UIShaderLibrary* lib) {
    if (!scene) return NULL;
    if (scene->ownsShaders && scene->shaders && scene->shaders != lib) {
        UIShader_DestroyLibrary(scene->shaders);
    }
    scene->shaders = lib;
    scene->ownsShaders = 0;
    return scene;
}

UIScene3D* UIScene3D_OwnShaderLibrary(UIScene3D* scene, UIShaderLibrary* lib) {
    if (!scene) return NULL;
    if (scene->ownsShaders && scene->shaders && scene->shaders != lib) {
        UIShader_DestroyLibrary(scene->shaders);
    }
    scene->shaders = lib;
    scene->ownsShaders = lib ? 1 : 0;
    return scene;
}

UIScene3D* UIScene3D_UseShader(UIScene3D* scene, const char* name) {
    if (!scene || !name) return scene;
    if (scene->shaders) UIShader_Use(scene->shaders, name);
    snprintf(scene->activeShader, sizeof(scene->activeShader), "%s", name);
    return scene;
}

UICamera3D* UIScene3D_Camera(UIScene3D* scene) {
    return scene ? &scene->camera : NULL;
}

UISprite3D* UIScene3D_AddSprite(UIScene3D* scene, const char* source,
                                float x, float y, float z,
                                float w, float h) {
    if (!scene || scene->spriteCount >= UI_SCENE_MAX_SPRITES) return NULL;
    UISprite3D* sp = &scene->sprites[scene->spriteCount++];
    memset(sp, 0, sizeof(*sp));
    sp->source = DupStr(source);
    sp->x = x; sp->y = y; sp->z = z;
    sp->w = w > 0 ? w : 32.0f;
    sp->h = h > 0 ? h : 32.0f;
    sp->opacity = 1.0f;
    sp->tint = (UIColor){ 255, 255, 255, 1.0f };
    sp->billboard = UI_BILLBOARD_Y_AXIS;
    sp->visible = 1;
    return sp;
}

UISprite3D* UIScene3D_SpriteAt(UIScene3D* scene, int index) {
    if (!scene || index < 0 || index >= scene->spriteCount) return NULL;
    return &scene->sprites[index];
}

int UIScene3D_SpriteCount(const UIScene3D* scene) {
    return scene ? scene->spriteCount : 0;
}

void UIScene3D_ClearSprites(UIScene3D* scene) {
    if (!scene) return;
    for (int i = 0; i < scene->spriteCount; i++) {
        free(scene->sprites[i].source);
        if (scene->sprites[i].texture) {
            SDL_DestroyTexture(scene->sprites[i].texture);
        }
    }
    scene->spriteCount = 0;
}

UIPointLight* UIScene3D_AddPointLight(UIScene3D* scene,
                                      float x, float y, float z,
                                      float radius, float intensity,
                                      UIColor color) {
    if (!scene || scene->lightCount >= UI_SCENE_MAX_LIGHTS) return NULL;
    UIPointLight* L = &scene->lights[scene->lightCount++];
    memset(L, 0, sizeof(*L));
    L->x = x; L->y = y; L->z = z;
    L->radius = radius > 0 ? radius : 128.0f;
    L->intensity = intensity > 0 ? intensity : 1.0f;
    L->color = color;
    L->enabled = 1;
    return L;
}

UIPointLight* UIScene3D_LightAt(UIScene3D* scene, int index) {
    if (!scene || index < 0 || index >= scene->lightCount) return NULL;
    return &scene->lights[index];
}

int UIScene3D_LightCount(const UIScene3D* scene) {
    return scene ? scene->lightCount : 0;
}

UIMesh3D* UIScene3D_AddMesh(UIScene3D* scene,
                            const float* positions, int vertexCount,
                            const uint32_t* indices, int indexCount) {
    if (!scene || !positions || vertexCount < 3
        || scene->meshCount >= UI_SCENE_MAX_MESHES) return NULL;
    UIMesh3D* m = &scene->meshes[scene->meshCount++];
    memset(m, 0, sizeof(*m));
    size_t bytes = (size_t)vertexCount * 3 * sizeof(float);
    m->positions = (float*)malloc(bytes);
    if (!m->positions) {
        scene->meshCount--;
        return NULL;
    }
    memcpy(m->positions, positions, bytes);
    m->vertexCount = vertexCount;
    if (indices && indexCount > 0) {
        m->indices = (uint32_t*)malloc((size_t)indexCount * sizeof(uint32_t));
        if (m->indices) {
            memcpy(m->indices, indices, (size_t)indexCount * sizeof(uint32_t));
            m->indexCount = indexCount;
        }
    }
    m->scale = 1.0f;
    m->color = (UIColor){ 220, 220, 220, 1.0f };
    m->visible = 1;
    return m;
}

UIMesh3D* UIScene3D_MeshAt(UIScene3D* scene, int index) {
    if (!scene || index < 0 || index >= scene->meshCount) return NULL;
    return &scene->meshes[index];
}

int UIScene3D_MeshCount(const UIScene3D* scene) {
    return scene ? scene->meshCount : 0;
}

/* ---- lighting / sort helpers ---- */

typedef struct {
    int index;
    float depth;
    float y;
} SortKey;

static int CmpSortKey(const void* a, const void* b) {
    const SortKey* ka = (const SortKey*)a;
    const SortKey* kb = (const SortKey*)b;
    if (ka->depth < kb->depth) return -1;
    if (ka->depth > kb->depth) return 1;
    if (ka->y < kb->y) return -1;
    if (ka->y > kb->y) return 1;
    return 0;
}

static void AccumLight(const UIScene3D* scene, float x, float y, float z,
                       float* outR, float* outG, float* outB) {
    float r = 0.35f, g = 0.35f, b = 0.40f; /* ambient */
    const char* prog = scene->activeShader;
    if (scene->shaders) {
        const char* a = UIShader_Active(scene->shaders);
        if (a) prog = a;
    }
    int lit = (strcmp(prog, "unlit") != 0);
    if (!lit || scene->mode == UI_GFX_MODE_2D) {
        *outR = 1.0f; *outG = 1.0f; *outB = 1.0f;
        return;
    }
    for (int i = 0; i < scene->lightCount; i++) {
        const UIPointLight* L = &scene->lights[i];
        if (!L->enabled) continue;
        float dx = x - L->x, dy = y - L->y, dz = z - L->z;
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if (dist >= L->radius) continue;
        float t = 1.0f - dist / L->radius;
        t = t * t;
        float inten = t * L->intensity;
        r += (L->color.r / 255.0f) * inten;
        g += (L->color.g / 255.0f) * inten;
        b += (L->color.b / 255.0f) * inten;
    }
    /* hd2d rim — slight lift so sprites read against dark floors */
    if (strcmp(prog, "hd2d") == 0) {
        r += 0.08f; g += 0.07f; b += 0.10f;
    }
    if (r > 1.5f) r = 1.5f;
    if (g > 1.5f) g = 1.5f;
    if (b > 1.5f) b = 1.5f;
    *outR = r; *outG = g; *outB = b;
}

static void EnsureSpriteTexture(UISprite3D* sp, SDL_Renderer* r) {
    if (!sp || sp->texture || !sp->source || !r) return;
    sp->texture = UIAsset_LoadTexture(r, sp->source);
    if (!sp->texture) {
        UI_WARN(UI_CAT_RENDER, "UIScene3D: failed to load '%s'", sp->source);
    }
}

void UIScene3D_Render(UIScene3D* scene, SDL_Renderer* renderer,
                      float originX, float originY, float opacity) {
    if (!scene || !renderer || opacity <= 0.0f) return;

    if (scene->background.a > 0.0f) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer,
                               (Uint8)scene->background.r,
                               (Uint8)scene->background.g,
                               (Uint8)scene->background.b,
                               (Uint8)(scene->background.a * opacity * 255.0f));
        SDL_FRect bg = { originX, originY, scene->width, scene->height };
        SDL_RenderFillRect(renderer, &bg);
    }

    const UICamera3D* cam = &scene->camera;
    float zoom = cam->zoom > 0.01f ? cam->zoom : 1.0f;
    float parallax = (scene->mode == UI_GFX_MODE_2D) ? 0.0f : 0.35f;

    /* ---- sprites ---- */
    SortKey keys[UI_SCENE_MAX_SPRITES];
    int n = 0;
    for (int i = 0; i < scene->spriteCount; i++) {
        if (!scene->sprites[i].visible) continue;
        keys[n].index = i;
        keys[n].depth = scene->sprites[i].z;
        keys[n].y = scene->sprites[i].y;
        n++;
    }
    qsort(keys, (size_t)n, sizeof(SortKey), CmpSortKey);

    for (int k = 0; k < n; k++) {
        UISprite3D* sp = &scene->sprites[keys[k].index];
        EnsureSpriteTexture(sp, renderer);
        if (!sp->texture) continue;

        float sx = (sp->x - cam->x) * zoom;
        float sy = (sp->y - cam->y) * zoom;
        sy += sp->z * parallax * zoom;

        float lr, lg, lb;
        AccumLight(scene, sp->x, sp->y, sp->z, &lr, &lg, &lb);

        Uint8 cr = (Uint8)fminf(255.0f, (float)sp->tint.r * lr);
        Uint8 cg = (Uint8)fminf(255.0f, (float)sp->tint.g * lg);
        Uint8 cb = (Uint8)fminf(255.0f, (float)sp->tint.b * lb);
        Uint8 ca = (Uint8)fminf(255.0f, sp->opacity * opacity * sp->tint.a * 255.0f);

        SDL_SetTextureColorMod(sp->texture, cr, cg, cb);
        SDL_SetTextureAlphaMod(sp->texture, ca);
        SDL_SetTextureBlendMode(sp->texture, SDL_BLENDMODE_BLEND);

        float angle = sp->rotation;
        if (sp->billboard == UI_BILLBOARD_Y_AXIS) {
            angle -= cam->yaw; /* keep upright relative to yaw */
        } else if (sp->billboard == UI_BILLBOARD_SPHERICAL) {
            angle -= cam->yaw;
            /* pitch foreshorten */
            sy -= sinf(cam->pitch * 0.0174532925f) * sp->h * 0.15f;
        }

        SDL_FRect dst = {
            originX + sx - sp->w * 0.5f * zoom,
            originY + sy - sp->h * zoom,
            sp->w * zoom,
            sp->h * zoom
        };
        SDL_FPoint center = { dst.w * 0.5f, dst.h };
        SDL_RenderTextureRotated(renderer, sp->texture, NULL, &dst,
                                 angle, &center, SDL_FLIP_NONE);

        if (sp->castsShadow && scene->mode != UI_GFX_MODE_2D) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, (Uint8)(40 * opacity));
            SDL_FRect shadow = {
                dst.x + dst.w * 0.15f,
                originY + sy + 2.0f,
                dst.w * 0.7f,
                fmaxf(4.0f, dst.h * 0.12f)
            };
            SDL_RenderFillRect(renderer, &shadow);
        }
    }

    /* ---- meshes (flat fill; GPU path later) ---- */
    for (int i = 0; i < scene->meshCount; i++) {
        UIMesh3D* m = &scene->meshes[i];
        if (!m->visible || !m->positions) continue;
        float lr, lg, lb;
        AccumLight(scene, m->x, m->y, m->z, &lr, &lg, &lb);
        Uint8 cr = (Uint8)fminf(255.0f, (float)m->color.r * lr);
        Uint8 cg = (Uint8)fminf(255.0f, (float)m->color.g * lg);
        Uint8 cb = (Uint8)fminf(255.0f, (float)m->color.b * lb);
        Uint8 ca = (Uint8)(m->color.a * opacity * 255.0f);
        SDL_SetRenderDrawColor(renderer, cr, cg, cb, ca);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        int triCount = m->indexCount > 0 ? m->indexCount / 3
                                         : m->vertexCount / 3;
        for (int t = 0; t < triCount; t++) {
            uint32_t i0, i1, i2;
            if (m->indices && m->indexCount >= (t + 1) * 3) {
                i0 = m->indices[t * 3 + 0];
                i1 = m->indices[t * 3 + 1];
                i2 = m->indices[t * 3 + 2];
            } else {
                i0 = (uint32_t)(t * 3 + 0);
                i1 = (uint32_t)(t * 3 + 1);
                i2 = (uint32_t)(t * 3 + 2);
            }
            if ((int)i0 >= m->vertexCount || (int)i1 >= m->vertexCount
                || (int)i2 >= m->vertexCount) continue;
            SDL_FPoint pts[3];
            for (int v = 0; v < 3; v++) {
                uint32_t idx = (v == 0) ? i0 : (v == 1) ? i1 : i2;
                float* p = &m->positions[idx * 3];
                float px = (p[0] * m->scale + m->x - cam->x) * zoom;
                float py = (p[1] * m->scale + m->y - cam->y) * zoom;
                py += (p[2] * m->scale + m->z) * parallax * zoom;
                pts[v].x = originX + px;
                pts[v].y = originY + py;
            }
            SDL_RenderLine(renderer, pts[0].x, pts[0].y, pts[1].x, pts[1].y);
            SDL_RenderLine(renderer, pts[1].x, pts[1].y, pts[2].x, pts[2].y);
            SDL_RenderLine(renderer, pts[2].x, pts[2].y, pts[0].x, pts[0].y);
        }
    }
}

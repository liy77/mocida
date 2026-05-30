#ifndef UIKIT_BUNDLE_H
#define UIKIT_BUNDLE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Virtual asset bundling.
 *
 * Map "mocida://name" URIs to real files so the same app code finds its
 * fonts / images / etc. both on desktop (where the file sits at a dev
 * path) and inside a sandboxed iOS app bundle (where the build copied it
 * under assets/). Anything that takes a path — UIGetFont, UIImage_Create,
 * UIAsset_* — accepts a mocida:// URI and resolves it transparently.
 *
 * Two ways to register assets:
 *   1. In code:   UIApp_SetBundle("mocida://font.ttf", "assets/Inter.ttf");
 *   2. Declaratively, via an app.bundle JSON manifest that UIApp_Create
 *      auto-loads:
 *
 *        {
 *          "name": "My App",
 *          "id":   "net.liy77.myapp",
 *          "assets": {
 *            "font.ttf":  "assets/Inter.ttf",
 *            "logo.png":  "assets/logo.png"
 *          }
 *        }
 *
 * The manifest's "name" becomes the app/window display name and "id" the
 * bundle identifier (on iOS these also drive CFBundleDisplayName / the
 * bundle id at build time — see build.py --ios).
 */

#define UI_BUNDLE_SCHEME "mocida://"

/**
 * Registers a virtual asset. `virtualUri` may include the scheme
 * ("mocida://font.ttf") or be the bare key ("font.ttf"). `realPath` is the
 * source file (a dev path, or one relative to the CWD / executable). Both
 * strings are copied. Re-registering a key replaces it.
 */
void UIApp_SetBundle(const char* virtualUri, const char* realPath);

/**
 * Resolves a possibly-virtual path to a real, openable path:
 *   - A non-"mocida://" path is returned unchanged (pass-through).
 *   - A "mocida://key" is resolved to the registered source if it exists,
 *     else to <bundle>/assets/key, else <bundle>/key (the iOS layout).
 * Returns a stable borrowed string owned by the registry, or NULL if a
 * mocida:// key cannot be resolved to an existing file.
 */
const char* UIApp_ResolveBundle(const char* uri);

/**
 * Loads an app.bundle manifest (see format above). Registers its assets,
 * and adopts its name / id. Returns 1 on success, 0 on failure. Called
 * automatically by UIApp_Create for ./app.bundle (and one next to the
 * executable); call it explicitly to load a manifest from elsewhere.
 */
int UIApp_LoadBundleManifest(const char* path);

/**
 * App / bundle display name and identifier. The name is shown as the app
 * (window) name; the id is the bundle identifier. Settable in code or via
 * the manifest. Getters return NULL until set.
 */
void        UIApp_SetBundleName(const char* name);
const char* UIApp_GetBundleName(void);
const char* UIApp_GetBundleId(void);

/** Frees the bundle registry. Called from UIApp_Destroy. */
void UIApp_BundleShutdown(void);

#ifdef __cplusplus
}
#endif

#endif // UIKIT_BUNDLE_H

// svg2ico — converts an SVG into a Windows ICO file with PNG-encoded
// entries at the standard taskbar / explorer / jumbo sizes. Used at
// build time so the installer (and any other Mocida exe that wants
// it) can carry its icon as a Windows resource — visible in Explorer
// before the process even launches.
//
// Usage:
//     svg2ico  <input.svg>  <output.ico>
//
// The ICO format header & directory entries are 22 bytes per icon;
// each payload is the raw PNG bytes for that size. Windows Vista+
// understands PNG-encoded entries (a single 256x256 PNG entry weighs
// far less than the equivalent uncompressed BMP entry).

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const int kSizes[] = { 16, 24, 32, 48, 64, 128, 256 };
static const int kSizesN = (int)(sizeof(kSizes) / sizeof(kSizes[0]));

typedef struct {
    int size;
    unsigned char* pngData;
    size_t pngLen;
} IconEntry;

static int RenderAndEncode(const unsigned char* svgBytes, size_t svgSize,
                           int target, const char* tmpDir,
                           IconEntry* out) {
    SDL_IOStream* mem = SDL_IOFromConstMem(svgBytes, svgSize);
    if (!mem) return 0;
    SDL_Surface* surf = IMG_LoadSizedSVG_IO(mem, target, target);
    SDL_CloseIO(mem);
    if (!surf) {
        fprintf(stderr, "svg2ico: SVG render at %dx%d failed: %s\n",
                target, target, SDL_GetError());
        return 0;
    }

    // Encode the surface as PNG via a temp file. SDL3_image has
    // IMG_SavePNG_IO which works in-memory too, but going through a
    // file keeps the code straightforward and the temp lifetime
    // visible — these are tiny (16-256 px) files.
    char tmpPath[1024];
    snprintf(tmpPath, sizeof(tmpPath), "%s\\svg2ico_%d.png", tmpDir, target);
    if (!IMG_SavePNG(surf, tmpPath)) {
        fprintf(stderr, "svg2ico: IMG_SavePNG failed: %s\n", SDL_GetError());
        SDL_DestroySurface(surf);
        return 0;
    }
    SDL_DestroySurface(surf);

    FILE* fp = fopen(tmpPath, "rb");
    if (!fp) { fprintf(stderr, "svg2ico: reopen %s failed\n", tmpPath); return 0; }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    unsigned char* buf = (unsigned char*)malloc((size_t)sz);
    if (!buf) { fclose(fp); return 0; }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf); fclose(fp); return 0;
    }
    fclose(fp);
    remove(tmpPath);

    out->size    = target;
    out->pngData = buf;
    out->pngLen  = (size_t)sz;
    return 1;
}

// Little-endian 16/32-bit writers — ICO is LE on every Windows host.
static void put_u16(unsigned char* p, uint16_t v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}
static void put_u32(unsigned char* p, uint32_t v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: svg2ico <input.svg> <output.ico>\n");
        return 2;
    }
    const char* inPath  = argv[1];
    const char* outPath = argv[2];

    if (!SDL_Init(0)) {
        fprintf(stderr, "svg2ico: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // Read the entire SVG into memory once and reuse it across sizes.
    FILE* fp = fopen(inPath, "rb");
    if (!fp) {
        fprintf(stderr, "svg2ico: cannot open %s\n", inPath);
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    long svgSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    unsigned char* svgBytes = (unsigned char*)malloc((size_t)svgSize);
    if (!svgBytes) { fclose(fp); return 1; }
    if (fread(svgBytes, 1, (size_t)svgSize, fp) != (size_t)svgSize) {
        fprintf(stderr, "svg2ico: short read on %s\n", inPath);
        free(svgBytes); fclose(fp); return 1;
    }
    fclose(fp);

    // Derive a temp directory from the output path's parent so we
    // don't depend on %TEMP% / TMP being set in the build env.
    char tmpDir[1024];
    {
        snprintf(tmpDir, sizeof(tmpDir), "%s", outPath);
        char* slash = strrchr(tmpDir, '\\');
        if (!slash) slash = strrchr(tmpDir, '/');
        if (slash) *slash = 0; else snprintf(tmpDir, sizeof(tmpDir), ".");
    }

    IconEntry entries[16] = {0};
    int n = 0;
    for (int i = 0; i < kSizesN; i++) {
        if (RenderAndEncode(svgBytes, (size_t)svgSize, kSizes[i], tmpDir,
                            &entries[n])) {
            n++;
        }
    }
    free(svgBytes);
    if (n == 0) {
        fprintf(stderr, "svg2ico: no sizes rendered, aborting.\n");
        return 1;
    }

    // Compose the ICO file:
    //   header (6B) + n * dir entry (16B) + n * PNG payload.
    FILE* out = fopen(outPath, "wb");
    if (!out) {
        fprintf(stderr, "svg2ico: cannot open %s for write\n", outPath);
        return 1;
    }

    unsigned char hdr[6];
    put_u16(hdr + 0, 0);     // reserved
    put_u16(hdr + 2, 1);     // type = 1 (icon)
    put_u16(hdr + 4, (uint16_t)n);
    fwrite(hdr, 1, sizeof(hdr), out);

    uint32_t dataOffset = (uint32_t)(sizeof(hdr) + n * 16);
    for (int i = 0; i < n; i++) {
        const IconEntry* e = &entries[i];
        // 256 is encoded as 0 in the byte-sized width/height fields.
        unsigned char w = (e->size == 256) ? 0 : (unsigned char)e->size;
        unsigned char entry[16] = {0};
        entry[0] = w;            // width
        entry[1] = w;            // height
        entry[2] = 0;            // colors (0 = >= 256)
        entry[3] = 0;            // reserved
        put_u16(entry + 4, 1);   // color planes
        put_u16(entry + 6, 32);  // bits per pixel
        put_u32(entry + 8,  (uint32_t)e->pngLen);
        put_u32(entry + 12, dataOffset);
        fwrite(entry, 1, sizeof(entry), out);
        dataOffset += (uint32_t)e->pngLen;
    }
    for (int i = 0; i < n; i++) {
        fwrite(entries[i].pngData, 1, entries[i].pngLen, out);
        free(entries[i].pngData);
    }
    fclose(out);

    fprintf(stdout, "svg2ico: wrote %s with %d sizes.\n", outPath, n);
    return 0;
}

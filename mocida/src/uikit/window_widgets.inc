// window_widgets.inc — #included by window_render.inc (SAME translation
// unit). One RenderWidget_<Type> function per widget kind, split out of the
// formerly ~2700-line RenderSingleWidget_Inner so each widget's draw code is
// editable in isolation. Each takes the resolved (window, el, base, op);
// containers recurse via the RenderSingleWidget forward-declared in
// window_render.inc. Relies on file-level state + the Draw* primitives from
// window_draw.inc.

static void RenderWidget_Stack(UIWindow* window, UIWidget* el,
                                UIWidgetBase* base, float op) {

        UIStack* s = (UIStack*)base;
        if (!s->items) return;
        UIChildren_SortByZ(s->items);
        // Optional background fill / border behind the stack's items.
        if (el->width && el->height &&
            (s->bgColor.a > 0.0f || s->borderWidth > 0.0f)) {
            g_tempRect1.x = el->x;
            g_tempRect1.y = el->y;
            g_tempRect1.w = *el->width;
            g_tempRect1.h = *el->height;
            UIColor fillC   = s->bgColor;     fillC.a   *= op;
            UIColor borderC = s->borderColor; borderC.a *= op;
            if (g_tempRect1.w > 0 && g_tempRect1.h > 0) {
                DrawRoundedRectWithBorder(window->sdlRenderer, g_tempRect1,
                                          fillC, s->radius,
                                          (int)s->borderWidth, borderC);
            }
        }
        // Cross-axis extents (inside padding) for `align` (center/end).
        const float innerW = (el->width  ? *el->width  : 0.0f) - s->paddingLeft - s->paddingRight;
        const float innerH = (el->height ? *el->height : 0.0f) - s->paddingTop  - s->paddingBottom;
        // Main-axis distribution (`justify`): measure the items' total extent +
        // gaps, then offset the start cursor (center/end) or spread the gap
        // (spaceBetween). Needs the stack to be bigger than its content on the
        // main axis (explicit size or runtime parent-fill); otherwise no room.
        // Items also carry outer margins; they count toward the main-axis
        // extent (justify) and offset each item on both axes.
        float mainStart = 0.0f, betweenGap = 0.0f;
        if (s->justify != UI_STACK_JUSTIFY_START) {
            int   visN     = 0;
            float mainUsed = 0.0f;
            for (int i = 0; i < s->items->count; i++) {
                UIWidget* it = s->items->children[i];
                if (!it || !it->visible) continue;
                mainUsed += (s->orientation == UI_STACK_HORIZONTAL)
                          ? (it->width  ? *it->width  : 0.0f) + it->marginLeft + it->marginRight
                          : (it->height ? *it->height : 0.0f) + it->marginTop  + it->marginBottom;
                visN++;
            }
            const float mainAvail = (s->orientation == UI_STACK_HORIZONTAL) ? innerW : innerH;
            if (s->justify == UI_STACK_JUSTIFY_BETWEEN) {
                if (visN > 1) betweenGap = (mainAvail - mainUsed) / (float)(visN - 1);
                if (betweenGap < s->spacing) betweenGap = s->spacing;
            } else {
                const float content = mainUsed + s->spacing * (float)(visN > 0 ? visN - 1 : 0);
                float slack = mainAvail - content;
                if (slack < 0.0f) slack = 0.0f;
                mainStart = (s->justify == UI_STACK_JUSTIFY_CENTER) ? slack * 0.5f : slack;
            }
        }
        const float gap = (s->justify == UI_STACK_JUSTIFY_BETWEEN) ? betweenGap : s->spacing;
        float cursorX = el->x + s->paddingLeft + ((s->orientation == UI_STACK_HORIZONTAL) ? mainStart : 0.0f);
        float cursorY = el->y + s->paddingTop  + ((s->orientation == UI_STACK_HORIZONTAL) ? 0.0f : mainStart);
        for (int i = 0; i < s->items->count; i++) {
            UIWidget* item = s->items->children[i];
            if (!item || !item->visible) continue;
            const float iw = item->width  ? *item->width  : 0.0f;
            const float ih = item->height ? *item->height : 0.0f;
            const float mL = item->marginLeft, mT = item->marginTop;
            const float mR = item->marginRight, mB = item->marginBottom;
            // A child's `selfAlign` (1=start,2=center,3=end) overrides the
            // stack's `align` for that child only; 0 = inherit the stack.
            const int ea = item->selfAlign ? (item->selfAlign - 1) : s->align;
            if (s->orientation == UI_STACK_HORIZONTAL) {
                const float ew = ih + mT + mB; // cross extent incl. margins
                float off = (ea == UI_STACK_ALIGN_CENTER) ? (innerH - ew) * 0.5f
                          : (ea == UI_STACK_ALIGN_END)    ? (innerH - ew)
                          :                                  0.0f;
                if (off < 0.0f) off = 0.0f;
                cursorX += mL;                 // leading main-axis margin
                item->x = cursorX;
                item->y = cursorY + off + mT;  // cross align + leading cross margin
                RenderSingleWidget(window, item);
                cursorX += iw + mR + gap;      // trailing main-axis margin + gap
            } else {
                const float ew = iw + mL + mR;
                float off = (ea == UI_STACK_ALIGN_CENTER) ? (innerW - ew) * 0.5f
                          : (ea == UI_STACK_ALIGN_END)    ? (innerW - ew)
                          :                                  0.0f;
                if (off < 0.0f) off = 0.0f;
                cursorY += mT;
                item->x = cursorX + off + mL;
                item->y = cursorY;
                RenderSingleWidget(window, item);
                cursorY += ih + mB + gap;
            }
        }
}

static void RenderWidget_Dialog(UIWindow* window, UIWidget* el,
                                UIWidgetBase* base, float op) {

        UIDialog* d = (UIDialog*)base;
        if (!d->visible || !el->width || !el->height) return;

        SDL_FRect full = { el->x, el->y, *el->width, *el->height };
        UIColor backdrop = d->backdropColor; backdrop.a *= op;
        DrawRoundedRectFill(window->sdlRenderer, full, backdrop, 0.0f);

        const float cx = el->x + (*el->width  - d->cardW) * 0.5f;
        const float cy = el->y + (*el->height - d->cardH) * 0.5f;
        SDL_FRect card = { cx, cy, d->cardW, d->cardH };

        UIColor cc = d->cardColor; cc.a *= op;
        // Drop a shadow under the card so it pops off the backdrop.
        UIShadow dropSh = {
            .offsetX = 0.0f, .offsetY = 14.0f,
            .blur    = 32.0f, .spread = -6.0f,
            .color   = { 0, 0, 0, 0.4f * op }
        };
        DrawDropShadow(window->sdlRenderer, card, d->radius, dropSh);
        DrawRoundedRectWithBorder(window->sdlRenderer, card, cc, d->radius, 0,
                                  (UIColor){0,0,0,0.0f});

        // Content children: positioned relative to the card top-left.
        if (d->content) {
            for (int i = 0; i < d->content->count; i++) {
                UIWidget* c = d->content->children[i];
                if (!c) continue;
                // Store absolute coords during render so mouse hit-tests
                // and event handlers see them correctly.
                const float storedX = c->x;
                const float storedY = c->y;
                c->x = cx + storedX;
                c->y = cy + storedY;
                RenderSingleWidget(window, c);
                c->x = storedX;
                c->y = storedY;
            }
        }
}

static void RenderWidget_TabView(UIWindow* window, UIWidget* el,
                                UIWidgetBase* base, float op) {

        UITabView* tv = (UITabView*)base;
        if (!el->width || !el->height || tv->tabCount <= 0) return;

        const float W = *el->width;
        const float H = *el->height;
        const float headerH = tv->tabHeight;

        // Body panel underneath.
        SDL_FRect panel = { el->x, el->y + headerH, W, H - headerH };
        UIColor panelC = tv->panelBg; panelC.a *= op;
        DrawRoundedRectFill(window->sdlRenderer, panel, panelC, tv->radius);

        // Header row.
        const float tabW = W / (float)tv->tabCount;
        TTF_Font* font = (tv->fontFamily && tv->fontSize > 0.0f)
            ? GetFont(tv->fontFamily, tv->fontSize) : NULL;
        // Cached fonts are shared across all widgets — reset the style
        // bits so a Bold/Italic UIText rendered earlier doesn't leak
        // into these labels (TabView has no fontStyle field).
        if (font) TTF_SetFontStyle(font, TTF_STYLE_NORMAL);

        // If the shared font changed, drop every cached label texture so
        // they re-raster at the new face/size.
        if (tv->__titleFontCached != (void*)tv->fontFamily ||
            tv->__titleSizeCached != tv->fontSize) {
            for (int i = 0; i < tv->__titleCacheN; i++) {
                if (tv->__titleTex && tv->__titleTex[i]) {
                    SDL_DestroyTexture(tv->__titleTex[i]);
                    tv->__titleTex[i] = NULL;
                }
            }
            tv->__titleFontCached = (void*)tv->fontFamily;
            tv->__titleSizeCached = tv->fontSize;
        }
        // Grow the parallel cache arrays to tabCount (zero-initialised).
        if (tv->__titleCacheN < tv->tabCount) {
            SDL_Texture** nt = (SDL_Texture**)realloc(tv->__titleTex,
                                    sizeof(*nt) * tv->tabCount);
            int* na  = (int*)realloc(tv->__titleActiveCached, sizeof(int) * tv->tabCount);
            const char** ns = (const char**)realloc(tv->__titleStrCached,
                                    sizeof(*ns) * tv->tabCount);
            if (nt) tv->__titleTex = nt;
            if (na) tv->__titleActiveCached = na;
            if (ns) tv->__titleStrCached = ns;
            if (nt && na && ns) {
                for (int i = tv->__titleCacheN; i < tv->tabCount; i++) {
                    tv->__titleTex[i] = NULL;
                    tv->__titleActiveCached[i] = -1;
                    tv->__titleStrCached[i] = NULL;
                }
                tv->__titleCacheN = tv->tabCount;
            }
        }

        for (int i = 0; i < tv->tabCount; i++) {
            SDL_FRect h = { el->x + i * tabW, el->y, tabW, headerH };
            const int active = (i == tv->activeIndex);
            UIColor bg = active ? tv->tabBgActive : tv->tabBg; bg.a *= op;
            DrawRoundedRectFill(window->sdlRenderer, h, bg, tv->radius);

            if (font && tv->titles[i] && tv->__titleTex && i < tv->__titleCacheN) {
                // Rebuild this tab's label only when its text or active
                // state changed. The colour is baked at full opacity; the
                // per-frame `op` is applied via alpha-mod at blit so fading
                // a TabView doesn't invalidate the cache.
                if (!tv->__titleTex[i] ||
                    tv->__titleActiveCached[i] != active ||
                    tv->__titleStrCached[i] != tv->titles[i]) {
                    if (tv->__titleTex[i]) SDL_DestroyTexture(tv->__titleTex[i]);
                    tv->__titleTex[i] = NULL;
                    const UIColor txt = active ? tv->tabTextActive : tv->tabText;
                    SDL_Color sc = {(Uint8)txt.r,(Uint8)txt.g,(Uint8)txt.b,
                                    (Uint8)SDL_clamp((int)(txt.a*255.0f),0,255)};
                    const size_t len = strlen(tv->titles[i]);
                    SDL_Surface* surf = TTF_RenderText_Blended(font, tv->titles[i], len, sc);
                    if (surf) {
                        tv->__titleTex[i] = SDL_CreateTextureFromSurface(
                            window->sdlRenderer, surf);
                        SDL_DestroySurface(surf);
                        if (tv->__titleTex[i])
                            SDL_SetTextureScaleMode(tv->__titleTex[i], SDL_SCALEMODE_LINEAR);
                    }
                    tv->__titleActiveCached[i] = active;
                    tv->__titleStrCached[i] = tv->titles[i];
                }
                SDL_Texture* tex = tv->__titleTex[i];
                if (tex) {
                    float tw = 0, th = 0;
                    SDL_GetTextureSize(tex, &tw, &th);
                    SDL_FRect dst = { h.x + (tabW - tw) * 0.5f,
                                      h.y + (headerH - th) * 0.5f,
                                      tw, th };
                    SDL_SetTextureAlphaMod(tex, (Uint8)SDL_clamp((int)(op*255.0f),0,255));
                    SDL_RenderTexture(window->sdlRenderer, tex, NULL, &dst);
                    SDL_SetTextureAlphaMod(tex, 255);
                }
            }
        }

        // Active panel content.
        if (tv->activeIndex >= 0 && tv->activeIndex < tv->panels->count) {
            UIWidget* p = tv->panels->children[tv->activeIndex];
            if (p) {
                const float storedX = p->x;
                const float storedY = p->y;
                p->x = el->x + storedX;
                p->y = el->y + headerH + storedY;
                // If the panel has no explicit size, give it the body
                // area so its own renderer / children know the bounds.
                EnsureWidgetSize(p, W, H - headerH);
                RenderSingleWidget(window, p);
                p->x = storedX;
                p->y = storedY;
            }
        }
}

static void RenderWidget_Rectangle(UIWindow* window, UIWidget* el,
                                UIWidgetBase* base, float op) {

        UIRectangle *rect = (UIRectangle*)base;
        if (!el->width || !el->height) return;
        g_tempRect1.x = el->x;
        g_tempRect1.y = el->y;
        g_tempRect1.w = *el->width;
        g_tempRect1.h = *el->height;
        // Rectangle margins are now consumed by the alignment pass as
        // pure outer offsets (see UIAlignment_Align). Re-applying them
        // here would double-offset the geometry and — when margin >=
        // widget dimension — collapse the render to zero, which is
        // exactly what made anchored chips with a top margin invisible.
        // Apply opacity to the fill / border colours by scaling their alpha.
        UIColor fillC   = rect->color;       fillC.a   *= op;
        UIColor borderC = rect->borderColor; borderC.a *= op;
        UIShadow sh     = rect->shadow;      sh.color.a *= op;

        if (rect->hasShadow) {
            DrawDropShadow(window->sdlRenderer, g_tempRect1, rect->radius, sh);
        }
        if (g_tempRect1.w > 0 && g_tempRect1.h > 0) {
            // Rotated path: render the rect's shape into an offscreen
            // target at 4× resolution, then RenderTextureRotated it onto
            // the final destination. The 4× supersample + the linear
            // downsample during the rotated blit gives ~16:1 effective
            // AA per output pixel, enough to kill the diagonal stair-
            // step ("serrilhado") that even 2× still showed on the
            // rounded corners at the small rotation angles the logo
            // uses. Shadows are drawn upright by the call above — the
            // rect rotates over them.
            if (el->rotation != 0.0f) {
                // Rasterise the rotated rect with analytic per-pixel
                // coverage AA at the screen resolution. This avoids
                // the bilinear-sampling stair-step on the rounded
                // corners that texture rotation produces at small
                // angles. Border in rotation mode isn't supported yet
                // — fall back to the unrotated path if one is set.
                if (rect->borderWidth <= 0.0f) {
                    DrawRotatedRoundedRectFill(window->sdlRenderer,
                                               g_tempRect1,
                                               el->rotation,
                                               rect->radius,
                                               fillC);
                    return;
                }
                // Borderless fallback: draw upright if the caller
                // really did want a border + rotation.
            }
            if (fabsf(g_tempRect1.w - g_tempRect1.h) < 0.5f &&
                fabsf(rect->radius - g_tempRect1.w/2) < 0.5f) {
                float centerX = g_tempRect1.x + g_tempRect1.w/2;
                float centerY = g_tempRect1.y + g_tempRect1.h/2;
                float radius  = g_tempRect1.w/2;
                if (rect->borderWidth > 0) {
                    DrawCircle(window->sdlRenderer, centerX, centerY, radius, borderC);
                    radius -= rect->borderWidth;
                }
                if (radius > 0) {
                    DrawCircle(window->sdlRenderer, centerX, centerY, radius, fillC);
                }
            } else {
                DrawRoundedRectWithBorder(window->sdlRenderer, g_tempRect1,
                                          fillC, rect->radius,
                                          (int)rect->borderWidth, borderC);
            }
        }
        // Rectangle-as-container: render inner children top-to-bottom
        // inside the padding box. Children are absolutely positioned so
        // nested containers see on-screen coords. Each child keeps its
        // own opacity (mirrors the Stack render path).
        if (rect->children && ((UIChildren*)rect->children)->count > 0) {
            UIChildren* kids = (UIChildren*)rect->children;
            UIChildren_SortByZ(kids);
            float cursorX = el->x + rect->paddingLeft;
            float cursorY = el->y + rect->paddingTop;
            for (int i = 0; i < kids->count; i++) {
                UIWidget* item = kids->children[i];
                if (!item || !item->visible) continue;
                const float ih = item->height ? *item->height : 0.0f;
                // Honour the child's outer margins (vertical flow), so a child
                // can push itself away from the rect's edges / its neighbours.
                cursorY += item->marginTop;
                item->x = cursorX + item->marginLeft;
                item->y = cursorY;
                RenderSingleWidget(window, item);
                cursorY += ih + item->marginBottom + rect->gap;
            }
        }
}

static void RenderWidget_Text(UIWindow* window, UIWidget* el,
                                UIWidgetBase* base, float op) {

        UIText *twid = (UIText*)base;
        if (!twid->text || !twid->fontFamily || !*twid->text ||
            !twid->textLength || twid->fontSize <= 0.0f) return;

        // Resolve the effective wrap width. When wrapToBounds is set we
        // follow the widget's explicit width (minus padding); otherwise
        // we use the fixed wrapWidth.
        int effWrapW = twid->wrapWidth;
        if (twid->wrapToBounds && el->width && *el->width > 0.0f) {
            int contentW = (int)(*el->width - twid->paddingLeft - twid->paddingRight);
            if (contentW > 0) effWrapW = contentW;
        }
        // Pick a wrap mode. UI_WRAP_NONE with a positive wrapWidth keeps
        // the historical "wrap by word at fixed pixel width" behaviour.
        UIWrapMode effMode = twid->wrapMode;
        if (effMode == UI_WRAP_NONE && effWrapW > 0) effMode = UI_WRAP_WORD;

        // -------- Selectable path --------
        // Lays text out per visual line (so a hit-test can map a click
        // back to a byte offset) and draws a selection highlight on
        // top. FIT does not compose with selection (scaling would skew
        // mouse picking) and is treated as NONE here.
        if (twid->selectable) {
            UIWrapMode selMode = (effMode == UI_WRAP_FIT) ? UI_WRAP_NONE : effMode;
            int selWrapW = (selMode == UI_WRAP_NONE) ? -1 : effWrapW;
            if (selWrapW < 1) selWrapW = -1;

            TTF_Font *font = GetFont(twid->fontFamily, twid->fontSize);
            if (!font) return;
            // Hinting is set once at font load (GetFont); no need to
            // touch it per-render. Setting it again would force TTF to
            // rebuild internal glyph caches on a hit.
            TTF_SetFontStyle(font, (TTF_FontStyleFlags)twid->fontStyle);

            const int needRebuild =
                twid->__cachedTextLen  != twid->textLength ||
                twid->__cachedWrapMode != (int)selMode     ||
                twid->__cachedWrapW    != selWrapW;

            if (needRebuild) {
                if (twid->__lineTextures) {
                    for (int i = 0; i < twid->__linesLen; i++) {
                        if (twid->__lineTextures[i]) SDL_DestroyTexture(twid->__lineTextures[i]);
                    }
                }
                if (twid->__lineCharOffsets) {
                    for (int i = 0; i < twid->__linesLen; i++) {
                        free(twid->__lineCharOffsets[i]);
                    }
                }
                free(twid->__lineTextures);       twid->__lineTextures = NULL;
                free(twid->__lineStarts);         twid->__lineStarts = NULL;
                free(twid->__lineLengths);        twid->__lineLengths = NULL;
                free(twid->__lineCharOffsets);    twid->__lineCharOffsets = NULL;
                free(twid->__lineCharOffsetsLen); twid->__lineCharOffsetsLen = NULL;
                free(twid->__lineIsSoft);         twid->__lineIsSoft = NULL;
                twid->__linesLen = 0;

                int cap = 1;
                for (int i = 0; i < twid->textLength; i++) if (twid->text[i] == '\n') cap++;
                if (selWrapW > 0) cap = cap * 2 + 4;
                if (cap < 1) cap = 1;

                twid->__linesCap            = cap;
                twid->__lineTextures        = (SDL_Texture**)calloc(cap, sizeof(SDL_Texture*));
                twid->__lineStarts          = (int*)calloc(cap, sizeof(int));
                twid->__lineLengths         = (int*)calloc(cap, sizeof(int));
                twid->__lineCharOffsets     = (int**)calloc(cap, sizeof(int*));
                twid->__lineCharOffsetsLen  = (int*)calloc(cap, sizeof(int));
                twid->__lineIsSoft          = (int*)calloc(cap, sizeof(int));

                const UIColor c = twid->color;
                const SDL_Color sc = { (Uint8)c.r, (Uint8)c.g, (Uint8)c.b,
                                       (Uint8)SDL_clamp((int)(c.a * 255.0f), 0, 255) };

                int logicalStart = 0;
                for (int i = 0; i <= twid->textLength; i++) {
                    if (i < twid->textLength && twid->text[i] != '\n') continue;
                    const int lineEnd = i;
                    int segStart    = logicalStart;
                    int firstInLine = 1;

                    do {
                        int segEnd;
                        if (selWrapW <= 0 || lineEnd - segStart == 0) {
                            segEnd = lineEnd;
                        } else {
                            int w = 0, h = 0;
                            TTF_GetStringSize(font, twid->text + segStart,
                                              (size_t)(lineEnd - segStart), &w, &h);
                            if (w <= selWrapW) {
                                segEnd = lineEnd;
                            } else {
                                int lo = 1, hi = lineEnd - segStart, fit = 1;
                                while (lo <= hi) {
                                    int mid = (lo + hi) / 2;
                                    int ww = 0, hh = 0;
                                    TTF_GetStringSize(font, twid->text + segStart,
                                                      (size_t)mid, &ww, &hh);
                                    if (ww <= selWrapW) { fit = mid; lo = mid + 1; }
                                    else                 { hi = mid - 1; }
                                }
                                int cut = fit;
                                if (selMode == UI_WRAP_WORD) {
                                    int spAfter = -1;
                                    for (int k = cut; k > 0; k--) {
                                        char ch = twid->text[segStart + k - 1];
                                        if (ch == ' ' || ch == '\t') { spAfter = k; break; }
                                    }
                                    if (spAfter > 0) cut = spAfter;
                                }
                                if (cut <= 0) cut = 1;
                                segEnd = segStart + cut;
                            }
                        }

                        if (twid->__linesLen >= twid->__linesCap) {
                            int newCap = twid->__linesCap * 2;
                            if (newCap < twid->__linesLen + 1) newCap = twid->__linesLen + 8;
                            twid->__lineTextures        = (SDL_Texture**)realloc(twid->__lineTextures,        newCap * sizeof(SDL_Texture*));
                            twid->__lineStarts          = (int*)         realloc(twid->__lineStarts,          newCap * sizeof(int));
                            twid->__lineLengths         = (int*)         realloc(twid->__lineLengths,         newCap * sizeof(int));
                            twid->__lineCharOffsets     = (int**)        realloc(twid->__lineCharOffsets,     newCap * sizeof(int*));
                            twid->__lineCharOffsetsLen  = (int*)         realloc(twid->__lineCharOffsetsLen,  newCap * sizeof(int));
                            twid->__lineIsSoft          = (int*)         realloc(twid->__lineIsSoft,          newCap * sizeof(int));
                            for (int z = twid->__linesCap; z < newCap; z++) {
                                twid->__lineTextures[z]       = NULL;
                                twid->__lineStarts[z]         = 0;
                                twid->__lineLengths[z]        = 0;
                                twid->__lineCharOffsets[z]    = NULL;
                                twid->__lineCharOffsetsLen[z] = 0;
                                twid->__lineIsSoft[z]         = 0;
                            }
                            twid->__linesCap = newCap;
                        }

                        const int li     = twid->__linesLen;
                        const int segLen = segEnd - segStart;
                        twid->__lineStarts[li]  = segStart;
                        twid->__lineLengths[li] = segLen;
                        twid->__lineIsSoft[li]  = firstInLine ? 0 : 1;

                        if (segLen > 0) {
                            SDL_Surface* surf = TTF_RenderText_Blended(
                                font, twid->text + segStart, (size_t)segLen, sc);
                            if (surf) {
                                twid->__lineTextures[li] = SDL_CreateTextureFromSurface(
                                    window->sdlRenderer, surf);
                                SDL_DestroySurface(surf);
                                if (twid->__lineTextures[li]) {
                                    SDL_SetTextureScaleMode(twid->__lineTextures[li], SDL_SCALEMODE_LINEAR);
                                }
                            }
                        } else {
                            twid->__lineTextures[li] = NULL;
                        }

                        const int need = segLen + 1;
                        twid->__lineCharOffsetsLen[li] = need;
                        twid->__lineCharOffsets[li] = (int*)calloc(need, sizeof(int));
                        if (twid->__lineCharOffsets[li]) {
                            twid->__lineCharOffsets[li][0] = 0;
                            for (int k = 1; k < need; k++) {
                                int w = 0, h = 0;
                                TTF_GetStringSize(font, twid->text + segStart, (size_t)k, &w, &h);
                                twid->__lineCharOffsets[li][k] = w;
                            }
                        }

                        twid->__linesLen++;
                        firstInLine = 0;
                        segStart = segEnd;
                    } while (segStart < lineEnd);

                    logicalStart = lineEnd + 1;
                }

                twid->__cachedTextLen  = twid->textLength;
                twid->__cachedWrapMode = (int)selMode;
                twid->__cachedWrapW    = selWrapW;
            }

            // Compute the bounding box of the laid-out lines.
            const float lineH = twid->fontSize * 1.25f;
            float maxLineW = 0.0f;
            for (int li = 0; li < twid->__linesLen; li++) {
                if (!twid->__lineTextures[li]) continue;
                float lw = 0, lh = 0;
                SDL_GetTextureSize(twid->__lineTextures[li], &lw, &lh);
                if (lw > maxLineW) maxLineW = lw;
            }
            const float blockW = (selWrapW > 0) ? (float)selWrapW : maxLineW;
            const float blockH = lineH * (twid->__linesLen > 0 ? twid->__linesLen : 1);

            const float pw = blockW + twid->paddingLeft + twid->paddingRight;
            const float ph = blockH + twid->paddingTop  + twid->paddingBottom;

            float alignOffsetX = 0.0f;
            if (el->width && *el->width > pw) {
                if (twid->hAlign == UI_TEXT_HALIGN_CENTER) alignOffsetX = (*el->width - pw) * 0.5f;
                else if (twid->hAlign == UI_TEXT_HALIGN_RIGHT) alignOffsetX = (*el->width - pw);
            }
            float alignOffsetY = 0.0f;
            if (el->height && *el->height > ph) {
                if (twid->vAlign == UI_TEXT_VALIGN_CENTER) alignOffsetY = (*el->height - ph) * 0.5f;
                else if (twid->vAlign == UI_TEXT_VALIGN_BOTTOM) alignOffsetY = (*el->height - ph);
            }

            g_tempRect1.x = el->x + alignOffsetX;
            g_tempRect1.y = el->y + alignOffsetY;
            g_tempRect1.w = pw;
            g_tempRect1.h = ph;

            if (twid->background && twid->background->hasShadow) {
                UIShadow sh = twid->background->shadow; sh.color.a *= op;
                DrawDropShadow(window->sdlRenderer, g_tempRect1,
                               twid->background->radius, sh);
            }
            if (twid->background) {
                UIColor bgC     = twid->background->color;       bgC.a *= op;
                UIColor borderC = twid->background->borderColor; borderC.a *= op;
                DrawRoundedRectWithBorder(window->sdlRenderer, g_tempRect1,
                                          bgC, twid->background->radius,
                                          (int)twid->background->borderWidth,
                                          borderC);
            }

            const float originX = g_tempRect1.x + twid->paddingLeft;
            const float originY = g_tempRect1.y + twid->paddingTop;

            // Selection highlight (one rect per affected line).
            if (twid->selAnchor >= 0 && twid->selAnchor != twid->selCaret &&
                twid->__linesLen > 0) {
                int s = twid->selAnchor, e = twid->selCaret;
                if (s > e) { int t = s; s = e; e = t; }
                if (s < 0) s = 0;
                if (e > twid->textLength) e = twid->textLength;

                int sLine = 0, eLine = 0;
                { int lo = 0, hi = twid->__linesLen - 1;
                  while (lo < hi) {
                    int mid = (lo + hi + 1) / 2;
                    if (twid->__lineStarts[mid] <= s) lo = mid; else hi = mid - 1;
                  } sLine = lo; }
                { int lo = 0, hi = twid->__linesLen - 1;
                  while (lo < hi) {
                    int mid = (lo + hi + 1) / 2;
                    if (twid->__lineStarts[mid] <= e) lo = mid; else hi = mid - 1;
                  } eLine = lo; }

                UIColor selC = twid->selectionColor; selC.a *= op;
                for (int li = sLine; li <= eLine; li++) {
                    if (!twid->__lineCharOffsets[li]) continue;
                    int colStart = (li == sLine) ? s - twid->__lineStarts[li] : 0;
                    int colEnd   = (li == eLine) ? e - twid->__lineStarts[li]
                                                 : twid->__lineLengths[li];
                    if (colStart < 0) colStart = 0;
                    if (colEnd > twid->__lineLengths[li]) colEnd = twid->__lineLengths[li];
                    const float x1 = (float)twid->__lineCharOffsets[li][colStart];
                    const float x2 = (float)twid->__lineCharOffsets[li][colEnd];
                    const int isHardBreak = (li < eLine) &&
                        (li + 1 >= twid->__linesLen || !twid->__lineIsSoft[li + 1]);
                    const float tail = isHardBreak ? twid->fontSize * 0.4f : 0.0f;
                    SDL_FRect r = {
                        originX + x1,
                        originY + li * lineH,
                        (x2 - x1) + tail,
                        lineH
                    };
                    DrawRoundedRectFill(window->sdlRenderer, r, selC, 2.0f);
                }
            }

            // Draw the line glyph textures.
            for (int li = 0; li < twid->__linesLen; li++) {
                if (!twid->__lineTextures[li]) continue;
                float tw, th;
                SDL_GetTextureSize(twid->__lineTextures[li], &tw, &th);
                SDL_FRect dst = {
                    originX,
                    originY + li * lineH,
                    tw, th
                };
                SDL_SetTextureAlphaMod(twid->__lineTextures[li], (Uint8)(op * 255.0f + 0.5f));
                SDL_RenderTexture(window->sdlRenderer, twid->__lineTextures[li], NULL, &dst);
                SDL_SetTextureAlphaMod(twid->__lineTextures[li], 255);
            }
            return;
        }

        // Rebuild the cached texture when the width that drives wrap
        // changes (e.g. parent resized and we're following bounds). FIT
        // keeps the natural-size texture - scaling happens at draw time.
        if (twid->__SDL_textTexture && effMode != UI_WRAP_NONE &&
            effMode != UI_WRAP_FIT && effWrapW > 0) {
            float curW = 0.0f, curH = 0.0f;
            SDL_GetTextureSize(twid->__SDL_textTexture, &curW, &curH);
            if ((int)curW > effWrapW + 1) {
                SDL_DestroyTexture(twid->__SDL_textTexture);
                twid->__SDL_textTexture = NULL;
            }
        }

        if (!twid->__SDL_textTexture) {
            TTF_Font *font = GetFont(twid->fontFamily, twid->fontSize);
            if (font) {
                // Hinting set once at font load (GetFont).
                TTF_SetFontStyle(font, (TTF_FontStyleFlags)twid->fontStyle);
                SDL_Color sc = {(Uint8)twid->color.r,
                                (Uint8)twid->color.g,
                                (Uint8)twid->color.b,
                                (Uint8)SDL_clamp((int)(twid->color.a * 255), 0, 255)};
                SDL_Surface *surf = NULL;
                if (effMode == UI_WRAP_WORD && effWrapW > 0) {
                    surf = TTF_RenderText_Blended_Wrapped(font, twid->text,
                                                          twid->textLength, sc, effWrapW);
                } else if (effMode == UI_WRAP_FIT) {
                    // FIT renders single-line at natural size; the draw
                    // step below shrinks the dest rect to fit effWrapW.
                    surf = TTF_RenderText_Blended(font, twid->text,
                                                  twid->textLength, sc);
                } else if (effMode == UI_WRAP_CHAR && effWrapW > 0) {
                    // SDL_ttf doesn't ship a char-wrap mode; pre-insert
                    // '\n' between characters that would overflow and let
                    // the wrapped renderer respect them.
                    char* prepared = (char*)malloc((size_t)twid->textLength * 2 + 1);
                    if (prepared) {
                        int outLen = 0, lineStart = 0;
                        for (int i = 0; i < twid->textLength; i++) {
                            char ch = twid->text[i];
                            if (ch == '\n') {
                                prepared[outLen++] = '\n';
                                lineStart = outLen;
                                continue;
                            }
                            prepared[outLen] = ch;
                            int ww = 0, hh = 0;
                            TTF_GetStringSize(font, prepared + lineStart,
                                              (size_t)(outLen - lineStart + 1), &ww, &hh);
                            if (ww > effWrapW && outLen > lineStart) {
                                prepared[outLen++] = '\n';
                                lineStart = outLen;
                                prepared[outLen] = ch;
                            }
                            outLen++;
                        }
                        prepared[outLen] = '\0';
                        surf = TTF_RenderText_Blended_Wrapped(font, prepared,
                                                              (size_t)outLen, sc, 0x7FFFFFFF);
                        free(prepared);
                    }
                } else {
                    surf = TTF_RenderText_Blended(font, twid->text,
                                                  twid->textLength, sc);
                }
                if (surf) {
                    twid->__SDL_textTexture = SDL_CreateTextureFromSurface(
                        window->sdlRenderer, surf);
                    SDL_DestroySurface(surf);
                }
            }
        }
        if (!twid->__SDL_textTexture) return;

        SDL_SetTextureScaleMode(twid->__SDL_textTexture, SDL_SCALEMODE_LINEAR);
        float txw, txh;
        SDL_GetTextureSize(twid->__SDL_textTexture, &txw, &txh);

        // FIT: shrink the glyph block uniformly so it fits the
        // available width on a single line. Texture is rendered at the
        // natural size and the destination rectangle does the scaling.
        if (effMode == UI_WRAP_FIT && effWrapW > 0 && txw > (float)effWrapW) {
            const float s = (float)effWrapW / txw;
            txw *= s;
            txh *= s;
        }

        float pw = txw + twid->paddingLeft + twid->paddingRight;
        float ph = txh + twid->paddingTop + twid->paddingBottom;

        // Honor horizontal/vertical alignment when the widget has an
        // explicit size larger than the natural glyph block.
        float alignOffsetX = 0.0f;
        if (el->width && *el->width > pw) {
            switch (twid->hAlign) {
                case UI_TEXT_HALIGN_CENTER:
                    alignOffsetX = (*el->width - pw) * 0.5f;
                    break;
                case UI_TEXT_HALIGN_RIGHT:
                    alignOffsetX = (*el->width - pw);
                    break;
                case UI_TEXT_HALIGN_LEFT:
                default:
                    alignOffsetX = 0.0f;
                    break;
            }
        }

        float alignOffsetY = 0.0f;
        if (el->height && *el->height > ph) {
            switch (twid->vAlign) {
                case UI_TEXT_VALIGN_CENTER:
                    alignOffsetY = (*el->height - ph) * 0.5f;
                    break;
                case UI_TEXT_VALIGN_BOTTOM:
                    alignOffsetY = (*el->height - ph);
                    break;
                case UI_TEXT_VALIGN_TOP:
                default:
                    alignOffsetY = 0.0f;
                    break;
            }
        }

        g_tempRect1.x = el->x + alignOffsetX;
        g_tempRect1.y = el->y + alignOffsetY;
        g_tempRect1.w = pw;
        g_tempRect1.h = ph;

        // Opacity applied to background fill + border + shadow.
        if (twid->background && twid->background->hasShadow) {
            UIShadow sh = twid->background->shadow; sh.color.a *= op;
            DrawDropShadow(window->sdlRenderer, g_tempRect1,
                           twid->background->radius, sh);
        }

        if (twid->background) {
            UIColor bgC     = twid->background->color;       bgC.a *= op;
            UIColor borderC = twid->background->borderColor; borderC.a *= op;
            if (fabsf(twid->background->radius - g_tempRect1.w/2) < 0.5f &&
                fabsf(g_tempRect1.w - g_tempRect1.h) < 0.5f) {
                float centerX = g_tempRect1.x + g_tempRect1.w/2;
                float centerY = g_tempRect1.y + g_tempRect1.h/2;
                DrawCircle(window->sdlRenderer, centerX, centerY,
                           g_tempRect1.w/2, bgC);
            } else {
                DrawRoundedRectWithBorder(window->sdlRenderer, g_tempRect1,
                                          bgC, twid->background->radius,
                                          (int)twid->background->borderWidth,
                                          borderC);
            }
        }

        // Apply opacity to the glyph texture via alpha mod.
        SDL_SetTextureAlphaMod(twid->__SDL_textTexture, (Uint8)(op * 255.0f + 0.5f));

        g_tempRect2.x = g_tempRect1.x + twid->paddingLeft;
        g_tempRect2.y = g_tempRect1.y + twid->paddingTop;
        g_tempRect2.w = txw;
        g_tempRect2.h = txh;

        if (el->rotation != 0.0f) {
            SDL_FPoint pivot = { g_tempRect2.w * 0.5f, g_tempRect2.h * 0.5f };
            SDL_RenderTextureRotated(window->sdlRenderer, twid->__SDL_textTexture,
                                     NULL, &g_tempRect2,
                                     (double)el->rotation, &pivot, SDL_FLIP_NONE);
        } else {
            SDL_RenderTexture(window->sdlRenderer, twid->__SDL_textTexture,
                              NULL, &g_tempRect2);
        }
        // Reset alpha mod so subsequent users of the same texture aren't
        // affected by our opacity.
        SDL_SetTextureAlphaMod(twid->__SDL_textTexture, 255);
}

static void RenderWidget_Button(UIWindow* window, UIWidget* el,
                                UIWidgetBase* base, float op) {

        UIButton* btn = (UIButton*)base;
        if (!el->width || !el->height) return;

        g_tempRect1.x = el->x;
        g_tempRect1.y = el->y;
        g_tempRect1.w = *el->width;
        g_tempRect1.h = *el->height;
        ApplyMargins(&g_tempRect1, btn->marginLeft, btn->marginTop,
                                   btn->marginRight, btn->marginBottom);
        if (g_tempRect1.w <= 0 || g_tempRect1.h <= 0) return;

        const UIButtonStyle* style = &btn->styles[btn->state];
        // Apply widget opacity to fill / border / shadow / label.
        UIColor bgC     = style->background; bgC.a *= op;
        UIColor bdrC    = style->border;     bdrC.a *= op;
        UIColor textC   = style->text;       textC.a *= op;

        if (btn->background->hasShadow) {
            UIShadow sh = btn->background->shadow; sh.color.a *= op;
            DrawDropShadow(window->sdlRenderer, g_tempRect1,
                           btn->background->radius, sh);
        }
        DrawRoundedRectWithBorder(window->sdlRenderer, g_tempRect1,
                                  bgC, btn->background->radius,
                                  (int)btn->background->borderWidth, bdrC);

        if (btn->label && btn->label->text && *btn->label->text &&
            btn->label->fontFamily && btn->label->fontSize > 0.0f) {
            if (!btn->label->__SDL_textTexture) {
                TTF_Font* font = GetFont(btn->label->fontFamily, btn->label->fontSize);
                if (font) {
                    // Hinting set once at font load (GetFont).
                    TTF_SetFontStyle(font, (TTF_FontStyleFlags)btn->label->fontStyle);
                    const SDL_Color white = {255, 255, 255, 255};
                    SDL_Surface* surf = TTF_RenderText_Blended(
                        font, btn->label->text, btn->label->textLength, white);
                    if (surf) {
                        btn->label->__SDL_textTexture =
                            SDL_CreateTextureFromSurface(window->sdlRenderer, surf);
                        SDL_DestroySurface(surf);
                    }
                }
            }
            if (btn->label->__SDL_textTexture) {
                SDL_SetTextureScaleMode(btn->label->__SDL_textTexture, SDL_SCALEMODE_LINEAR);
                SDL_SetTextureColorMod(btn->label->__SDL_textTexture,
                                       (Uint8)textC.r,
                                       (Uint8)textC.g,
                                       (Uint8)textC.b);
                SDL_SetTextureAlphaMod(btn->label->__SDL_textTexture,
                                       (Uint8)SDL_clamp((int)(textC.a * 255.0f), 0, 255));
                float txw = 0.0f, txh = 0.0f;
                SDL_GetTextureSize(btn->label->__SDL_textTexture, &txw, &txh);
                const float pressOffset = (btn->state == UI_BUTTON_STATE_PRESSED) ? 1.0f : 0.0f;

                // Optical centring: SDL_ttf surfaces are ascent + descent
                // tall regardless of which glyphs are in them, so a
                // string like "Browse..." (no descenders) leaves an
                // empty band at the bottom of the bbox. Geometric centre
                // of the bbox therefore sits below the visual centre of
                // the ink — the text reads as drifting low. We pull it
                // back up by |descent|/2 to align the optical centre
                // with the button's vertical middle.
                float opticalShift = 0.0f;
                {
                    TTF_Font* metricFont = GetFont(btn->label->fontFamily,
                                                   btn->label->fontSize);
                    if (metricFont) {
                        int descent = TTF_GetFontDescent(metricFont); // negative
                        if (descent < 0) opticalShift = (float)(-descent) * 0.5f;
                    }
                }

                // Snap to the integer pixel grid. Centring an odd-width
                // glyph block in an even-width button lands the text on
                // a half-pixel, and LINEAR sampling then softens every
                // glyph. floor(... + 0.5) rounds to nearest int so the
                // blit is pixel-perfect regardless of widths.
                g_tempRect2.x = floorf(g_tempRect1.x + (g_tempRect1.w - txw) * 0.5f + 0.5f);
                g_tempRect2.y = floorf(g_tempRect1.y + (g_tempRect1.h - txh) * 0.5f
                                       - opticalShift + pressOffset + 0.5f);
                g_tempRect2.w = txw;
                g_tempRect2.h = txh;
                if (el->rotation != 0.0f) {
                    SDL_FPoint pivot = { txw * 0.5f, txh * 0.5f };
                    SDL_RenderTextureRotated(window->sdlRenderer, btn->label->__SDL_textTexture,
                                             NULL, &g_tempRect2,
                                             (double)el->rotation, &pivot, SDL_FLIP_NONE);
                } else {
                    SDL_RenderTexture(window->sdlRenderer, btn->label->__SDL_textTexture,
                                      NULL, &g_tempRect2);
                }
            }
        }
}

static void RenderWidget_Image(UIWindow* window, UIWidget* el,
                                UIWidgetBase* base, float op) {

        UIImage *img = (UIImage*)base;
        if (!img->source || !el->width || !el->height) return;
        if (!img->__SDL_texture && img->loadState != IMAGE_LOAD_FAILURE) {
            img->__SDL_texture = UIAsset_LoadTexture(window->sdlRenderer, img->source);
            if (img->__SDL_texture) {
                img->loadState = IMAGE_LOAD_SUCCESS;
                SDL_SetTextureScaleMode(img->__SDL_texture, SDL_SCALEMODE_LINEAR);
            } else {
                img->loadState = IMAGE_LOAD_FAILURE;
            }
        }
        if (!img->__SDL_texture) return;

        SDL_FRect bounds = { el->x, el->y, *el->width, *el->height };
        ApplyMargins(&bounds, img->marginLeft, img->marginTop,
                              img->marginRight, img->marginBottom);
        if (bounds.w <= 0 || bounds.h <= 0) return;

        // Tint colour combined with widget opacity. When the tint alpha
        // is 0 we treat it as "no tint" but still apply opacity.
        if (img->tintColor.a > 0.0f) {
            SDL_SetTextureColorMod(img->__SDL_texture,
                                   (Uint8)img->tintColor.r,
                                   (Uint8)img->tintColor.g,
                                   (Uint8)img->tintColor.b);
            const float a = img->tintColor.a * op;
            SDL_SetTextureAlphaMod(img->__SDL_texture,
                                   (Uint8)SDL_clamp((int)(a * 255.0f), 0, 255));
        } else {
            SDL_SetTextureColorMod(img->__SDL_texture, 255, 255, 255);
            SDL_SetTextureAlphaMod(img->__SDL_texture,
                                   (Uint8)SDL_clamp((int)(op * 255.0f), 0, 255));
        }

        float texW = 0.0f, texH = 0.0f;
        SDL_GetTextureSize(img->__SDL_texture, &texW, &texH);
        if (texW <= 0 || texH <= 0) return;

        SDL_FRect dst = bounds;
        int useClipForOverflow = 0;
        switch (img->fillMode) {
            case FILL_STRETCH:
            case FILL_SCALE:    dst = bounds; break;
            case FILL_NONE:
                dst.x = bounds.x; dst.y = bounds.y; dst.w = texW; dst.h = texH;
                useClipForOverflow = 1; break;
            case FILL_CENTER:
                dst.w = texW; dst.h = texH;
                dst.x = bounds.x + (bounds.w - texW) * 0.5f;
                dst.y = bounds.y + (bounds.h - texH) * 0.5f;
                useClipForOverflow = 1; break;
            case FILL_FIT: {
                const float s = SDL_min(bounds.w / texW, bounds.h / texH);
                dst.w = texW * s; dst.h = texH * s;
                dst.x = bounds.x + (bounds.w - dst.w) * 0.5f;
                dst.y = bounds.y + (bounds.h - dst.h) * 0.5f;
                break;
            }
            case FILL_FIT_WIDTH: {
                const float s = bounds.w / texW;
                dst.w = bounds.w; dst.h = texH * s;
                dst.x = bounds.x;
                dst.y = bounds.y + (bounds.h - dst.h) * 0.5f;
                break;
            }
            case FILL_FIT_HEIGHT: {
                const float s = bounds.h / texH;
                dst.w = texW * s; dst.h = bounds.h;
                dst.x = bounds.x + (bounds.w - dst.w) * 0.5f;
                dst.y = bounds.y;
                break;
            }
            case FILL_COVER: {
                const float s = SDL_max(bounds.w / texW, bounds.h / texH);
                dst.w = texW * s; dst.h = texH * s;
                dst.x = bounds.x + (bounds.w - dst.w) * 0.5f;
                dst.y = bounds.y + (bounds.h - dst.h) * 0.5f;
                useClipForOverflow = 1; break;
            }
            case FILL_TILE: {
                SDL_Rect clip2 = { (int)bounds.x, (int)bounds.y,
                                   (int)ceilf(bounds.w), (int)ceilf(bounds.h) };
                SDL_SetRenderClipRect(window->sdlRenderer, &clip2);
                for (float y = bounds.y; y < bounds.y + bounds.h; y += texH) {
                    for (float x = bounds.x; x < bounds.x + bounds.w; x += texW) {
                        SDL_FRect tile = { x, y, texW, texH };
                        SDL_RenderTexture(window->sdlRenderer,
                                          img->__SDL_texture, NULL, &tile);
                    }
                }
                SDL_SetRenderClipRect(window->sdlRenderer, NULL);
                return;
            }
            default: dst = bounds; break;
        }
        const int wantRound = (img->radius > 0.0f);
        const int wantBorder = (img->borderWidth > 0.0f);

        // Porter-Duff DST_IN: dst' = dst * src_alpha (both color + alpha).
        // Used as an alpha-mask blend on the rounded-shape texture so
        // pixels outside the rounded shape get zeroed out.
        const SDL_BlendMode kDstIn = SDL_ComposeCustomBlendMode(
            SDL_BLENDFACTOR_ZERO,            SDL_BLENDFACTOR_SRC_ALPHA,
            SDL_BLENDOPERATION_ADD,
            SDL_BLENDFACTOR_ZERO,            SDL_BLENDFACTOR_SRC_ALPHA,
            SDL_BLENDOPERATION_ADD);

        // DST_OUT: dst' = dst * (1 - src_alpha). Punches a hole in the
        // outer ring so the border becomes hollow.
        const SDL_BlendMode kDstOut = SDL_ComposeCustomBlendMode(
            SDL_BLENDFACTOR_ZERO,            SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            SDL_BLENDOPERATION_ADD,
            SDL_BLENDFACTOR_ZERO,            SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            SDL_BLENDOPERATION_ADD);

        const int tw = (int)ceilf(bounds.w);
        const int th = (int)ceilf(bounds.h);

        // Cache check: the rounded composite is expensive (multiple
        // render-target textures + target switches). Only rebuild it when
        // a signature input changed; otherwise re-blit the cached result.
        const int cacheValid =
            wantRound && img->__roundedCache &&
            img->__rcW == tw && img->__rcH == th &&
            img->__rcRadius   == img->radius &&
            img->__rcBorder   == img->borderWidth &&
            img->__rcRotation == el->rotation &&
            img->__rcFillMode == (int)img->fillMode &&
            img->__rcOp       == op &&
            img->__rcSrc      == (void*)img->__SDL_texture;

        if (cacheValid) {
            SDL_RenderTexture(window->sdlRenderer, img->__roundedCache, NULL, &bounds);
            return;
        }

        // Rebuild. The composite is rendered into `scratch`, which we KEEP
        // as the per-image cache (replacing any previous one) instead of
        // freeing it each frame.
        SDL_Texture* scratch = (wantRound && tw > 0 && th > 0)
            ? SDL_CreateTexture(window->sdlRenderer,
                SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, tw, th)
            : NULL;

        if (scratch) {
            SDL_SetTextureBlendMode(scratch, SDL_BLENDMODE_BLEND);
            SDL_Texture* prevTarget = SDL_GetRenderTarget(window->sdlRenderer);

            // (1) Image -> scratch.
            SDL_SetRenderTarget(window->sdlRenderer, scratch);
            SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(window->sdlRenderer, 0, 0, 0, 0);
            SDL_RenderClear(window->sdlRenderer);
            SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_BLEND);

            SDL_FRect localDst = { dst.x - bounds.x, dst.y - bounds.y, dst.w, dst.h };
            if (useClipForOverflow) {
                SDL_Rect clipLocal = { 0, 0, tw, th };
                SDL_SetRenderClipRect(window->sdlRenderer, &clipLocal);
            }
            if (el->rotation != 0.0f) {
                SDL_FPoint pivot = { localDst.w * 0.5f, localDst.h * 0.5f };
                SDL_RenderTextureRotated(window->sdlRenderer, img->__SDL_texture,
                                         NULL, &localDst,
                                         (double)el->rotation, &pivot, SDL_FLIP_NONE);
            } else {
                SDL_RenderTexture(window->sdlRenderer, img->__SDL_texture, NULL, &localDst);
            }
            if (useClipForOverflow) {
                SDL_SetRenderClipRect(window->sdlRenderer, NULL);
            }

            // (2) Rounded mask -> a separate texture, drawn with normal
            // blending so DrawRoundedRectFill's internal AA corner
            // textures composite correctly.
            SDL_Texture* mask = SDL_CreateTexture(window->sdlRenderer,
                SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, tw, th);
            if (mask) {
                SDL_SetTextureBlendMode(mask, SDL_BLENDMODE_BLEND);
                SDL_SetRenderTarget(window->sdlRenderer, mask);
                SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(window->sdlRenderer, 0, 0, 0, 0);
                SDL_RenderClear(window->sdlRenderer);
                SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_BLEND);

                SDL_FRect maskRect = { 0, 0, bounds.w, bounds.h };
                const UIColor white = { 255, 255, 255, 1.0f };
                DrawRoundedRectFill(window->sdlRenderer, maskRect, white, img->radius);

                // (3) Apply mask to scratch via DST_IN. SetTextureBlend
                // controls how the *texture* blends onto the render
                // target, so SDL's batched texture path honours it.
                SDL_SetRenderTarget(window->sdlRenderer, scratch);
                SDL_SetTextureBlendMode(mask, kDstIn);
                SDL_FRect full = { 0, 0, bounds.w, bounds.h };
                SDL_RenderTexture(window->sdlRenderer, mask, NULL, &full);

                SDL_DestroyTexture(mask);
            }

            // (4) Border ring on top of the masked image (still in scratch).
            if (wantBorder) {
                const float bw = img->borderWidth;
                SDL_Texture* ring = SDL_CreateTexture(window->sdlRenderer,
                    SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, tw, th);
                if (ring) {
                    SDL_SetTextureBlendMode(ring, SDL_BLENDMODE_BLEND);
                    SDL_SetRenderTarget(window->sdlRenderer, ring);
                    SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_NONE);
                    SDL_SetRenderDrawColor(window->sdlRenderer, 0, 0, 0, 0);
                    SDL_RenderClear(window->sdlRenderer);
                    SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_BLEND);

                    // Outer rounded shape filled with border colour.
                    UIColor bc = { 0, 0, 0, op };
                    SDL_FRect outer = { 0, 0, bounds.w, bounds.h };
                    DrawRoundedRectFill(window->sdlRenderer, outer, bc, img->radius);

                    // Inner hole.
                    if (outer.w - 2 * bw > 0 && outer.h - 2 * bw > 0) {
                        SDL_Texture* innerMask = SDL_CreateTexture(window->sdlRenderer,
                            SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, tw, th);
                        if (innerMask) {
                            SDL_SetTextureBlendMode(innerMask, SDL_BLENDMODE_BLEND);
                            SDL_SetRenderTarget(window->sdlRenderer, innerMask);
                            SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_NONE);
                            SDL_SetRenderDrawColor(window->sdlRenderer, 0, 0, 0, 0);
                            SDL_RenderClear(window->sdlRenderer);
                            SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_BLEND);

                            SDL_FRect inner = {
                                bw, bw, outer.w - 2 * bw, outer.h - 2 * bw
                            };
                            const float innerR = SDL_max(0.0f, img->radius - bw);
                            const UIColor whiteOpaque = { 255, 255, 255, 1.0f };
                            DrawRoundedRectFill(window->sdlRenderer, inner, whiteOpaque, innerR);

                            SDL_SetRenderTarget(window->sdlRenderer, ring);
                            SDL_SetTextureBlendMode(innerMask, kDstOut);
                            SDL_FRect rectFull = { 0, 0, bounds.w, bounds.h };
                            SDL_RenderTexture(window->sdlRenderer, innerMask, NULL, &rectFull);
                            SDL_DestroyTexture(innerMask);
                        }
                    }

                    // Composite the ring on top of scratch.
                    SDL_SetRenderTarget(window->sdlRenderer, scratch);
                    SDL_SetTextureBlendMode(ring, SDL_BLENDMODE_BLEND);
                    SDL_FRect ringRect = { 0, 0, bounds.w, bounds.h };
                    SDL_RenderTexture(window->sdlRenderer, ring, NULL, &ringRect);
                    SDL_DestroyTexture(ring);
                }
            }

            // (5) Store scratch as the per-image cache and blit it. The old
            // cache (if any) is replaced; the new one is reused next frame
            // until a signature input changes.
            SDL_SetRenderTarget(window->sdlRenderer, prevTarget);
            if (img->__roundedCache) SDL_DestroyTexture(img->__roundedCache);
            img->__roundedCache = scratch;
            img->__rcW = tw; img->__rcH = th;
            img->__rcRadius   = img->radius;
            img->__rcBorder   = img->borderWidth;
            img->__rcRotation = el->rotation;
            img->__rcFillMode = (int)img->fillMode;
            img->__rcOp       = op;
            img->__rcSrc      = (void*)img->__SDL_texture;
            SDL_RenderTexture(window->sdlRenderer, img->__roundedCache, NULL, &bounds);
        } else {
            // No rounded clip needed. Render image directly.
            if (useClipForOverflow) {
                SDL_Rect clip2 = { (int)bounds.x, (int)bounds.y,
                                   (int)ceilf(bounds.w), (int)ceilf(bounds.h) };
                SDL_SetRenderClipRect(window->sdlRenderer, &clip2);
            }
            if (el->rotation != 0.0f) {
                SDL_FPoint pivot = { dst.w * 0.5f, dst.h * 0.5f };
                SDL_RenderTextureRotated(window->sdlRenderer, img->__SDL_texture,
                                         NULL, &dst,
                                         (double)el->rotation, &pivot, SDL_FLIP_NONE);
            } else {
                SDL_RenderTexture(window->sdlRenderer, img->__SDL_texture, NULL, &dst);
            }
            if (useClipForOverflow) {
                SDL_SetRenderClipRect(window->sdlRenderer, NULL);
            }

            // Square (non-rounded) border drawn as four edge strips.
            if (wantBorder) {
                const float bw = img->borderWidth;
                const SDL_Color bc = {
                    0, 0, 0, (Uint8)SDL_clamp((int)(op * 255.0f), 0, 255)
                };
                SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(window->sdlRenderer,
                                       bc.r, bc.g, bc.b, bc.a);
                SDL_FRect edges[4] = {
                    { bounds.x, bounds.y, bounds.w, bw },
                    { bounds.x, bounds.y + bounds.h - bw, bounds.w, bw },
                    { bounds.x, bounds.y + bw, bw, bounds.h - 2 * bw },
                    { bounds.x + bounds.w - bw, bounds.y + bw, bw, bounds.h - 2 * bw }
                };
                for (int i = 0; i < 4; i++) {
                    if (edges[i].w > 0 && edges[i].h > 0) {
                        SDL_RenderFillRect(window->sdlRenderer, &edges[i]);
                    }
                }
            }
        }
}

static void RenderWidget_Video(UIWindow* window, UIWidget* el,
                                UIWidgetBase* base, float op) {

        UIVideo* v = (UIVideo*)base;
        if (!el->width || !el->height) return;

        // Decode any frames that are due for the current clock + push
        // queued audio.
        UIVideo_Tick(v, window->sdlRenderer);
        SDL_Texture* tex = UIVideo_GetTexture(v);
        if (!tex) return;

        const SDL_FRect bounds = { el->x, el->y, *el->width, *el->height };
        if (bounds.w <= 0 || bounds.h <= 0) return;

        const int videoW = UIVideo_GetWidth(v);
        const int videoH = UIVideo_GetHeight(v);
        if (videoW <= 0 || videoH <= 0) return;

        // Compute destination rect from fillMode (mirror of UIImage).
        SDL_FRect dst = bounds;
        int needClip = 0;
        UIFillMode fm = FILL_FIT;
        // Inspect the widget's internal fillMode via UIVideo getter would
        // be cleaner; for now we always letterbox (FILL_FIT) so the
        // aspect ratio is preserved. UIVideo_SetFillMode is a no-op for
        // render until we expose a getter.
        (void)fm;
        const float texW = (float)videoW;
        const float texH = (float)videoH;
        const float s = SDL_min(bounds.w / texW, bounds.h / texH);
        dst.w = texW * s;
        dst.h = texH * s;
        dst.x = bounds.x + (bounds.w - dst.w) * 0.5f;
        dst.y = bounds.y + (bounds.h - dst.h) * 0.5f;
        needClip = (dst.x < bounds.x || dst.y < bounds.y ||
                    dst.x + dst.w > bounds.x + bounds.w ||
                    dst.y + dst.h > bounds.y + bounds.h);

        if (needClip) {
            SDL_Rect clip = {
                (int)bounds.x, (int)bounds.y,
                (int)ceilf(bounds.w), (int)ceilf(bounds.h)
            };
            SDL_SetRenderClipRect(window->sdlRenderer, &clip);
        }
        SDL_SetTextureAlphaMod(tex, (Uint8)(op * 255.0f + 0.5f));
        SDL_RenderTexture(window->sdlRenderer, tex, NULL, &dst);
        SDL_SetTextureAlphaMod(tex, 255);
        if (needClip) {
            SDL_SetRenderClipRect(window->sdlRenderer, NULL);
        }
}

static void RenderWidget_Webview(UIWindow* window, UIWidget* el,
                                UIWidgetBase* base, float op) {

#ifdef _WIN32
        UIWebView* wv = (UIWebView*)base;
        if (!el->width || !el->height) return;
        if (!el->visible) return;

        // Pull the SDL window's HWND for parenting.
        HWND hwnd = NULL;
        if (window->sdlWindow) {
            SDL_PropertiesID props = SDL_GetWindowProperties(window->sdlWindow);
            hwnd = (HWND)SDL_GetPointerProperty(
                props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
        }

        // Walk siblings: any visible widget with a higher z that
        // overlaps the webview's bounds should be punched out of its
        // host HWND so it stays visible (the WebView2 child paints on
        // top of the SDL surface otherwise).
        //
        // We also pull the widget's corner radius (UIRectangle / UIButton)
        // so the punch matches the rendered shape - a rectangular punch
        // around a rounded card would leave the 4 corner triangles of
        // the bounding box showing the SDL background, visible as a
        // faint frame around the card.
        RECT clipBuf[32];
        int  clipRadii[32];
        int  clipCount = 0;
        const float wx = el->x, wy = el->y;
        const float ww = *el->width, wh = *el->height;
        if (window->children) {
            for (int i = 0; i < window->children->count; i++) {
                UIWidget* sib = window->children->children[i];
                if (!sib || sib == el || !sib->visible) continue;
                if (sib->z <= el->z) continue;
                if (!sib->width || !sib->height) continue;
                const float sx = sib->x;
                const float sy = sib->y;
                const float sw = *sib->width;
                const float sh = *sib->height;
                // Rect intersection test.
                if (sx + sw <= wx || sy + sh <= wy ||
                    sx >= wx + ww || sy >= wy + wh) continue;
                if (clipCount < (int)(sizeof(clipBuf)/sizeof(clipBuf[0]))) {
                    clipBuf[clipCount].left   = (LONG)sx;
                    clipBuf[clipCount].top    = (LONG)sy;
                    clipBuf[clipCount].right  = (LONG)(sx + sw);
                    clipBuf[clipCount].bottom = (LONG)(sy + sh);

                    // Best-effort radius probe by widget type. Text /
                    // mouse-areas / file-drops stay rectangular (0).
                    int radius = 0;
                    UIWidgetBase* sb = (UIWidgetBase*)sib->data;
                    if (sb && sb->__widget_type) {
                        if (!strcmp(sb->__widget_type, UI_WIDGET_RECTANGLE)) {
                            radius = (int)((UIRectangle*)sb)->radius;
                        } else if (!strcmp(sb->__widget_type, UI_WIDGET_BUTTON)) {
                            UIButton* btn = (UIButton*)sb;
                            if (btn->background) radius = (int)btn->background->radius;
                        }
                    }
                    clipRadii[clipCount] = radius;
                    clipCount++;
                }
            }
        }

        // Paint the configured border on the SDL surface BEFORE the
        // host HWND eats the area. UIWebView_RendererTick insets the
        // host by borderWidth, leaving the border ring visible.
        extern void UIWebView_GetVisuals(const UIWebView*, float*, float*,
                                         UIColor*, int*);
        float wvRadius = 0.0f, wvBorderW = 0.0f;
        UIColor wvBorderColor = { 0, 0, 0, 0.0f };
        int wvHasBorder = 0;
        UIWebView_GetVisuals(wv, &wvRadius, &wvBorderW, &wvBorderColor, &wvHasBorder);
        if (wvHasBorder && wvBorderW > 0.0f && window->sdlRenderer) {
            // Paint a hollow ring: outer rounded rect in border colour
            // followed by inner rounded rect in the window's clear
            // colour. We can't fill the whole bounding box with border
            // colour because the host HWND will be inset to expose the
            // ring - but higher-z widgets sitting on top of the
            // webview (overlays with their own rounded shape) punch
            // ROUNDED holes in the host HWND, and inside those holes
            // the SDL surface shows through. A solid border-colour
            // fill would leak through the overlay's bounding-box
            // corners (the triangles outside the rounded card body).
            SDL_FRect outer = { wx, wy, ww, wh };
            DrawRoundedRectFill(window->sdlRenderer, outer, wvBorderColor, wvRadius);
            SDL_FRect inner = {
                wx + wvBorderW, wy + wvBorderW,
                ww - 2.0f * wvBorderW, wh - 2.0f * wvBorderW
            };
            if (inner.w > 0.0f && inner.h > 0.0f) {
                float innerR = wvRadius - wvBorderW;
                if (innerR < 0.0f) innerR = 0.0f;
                DrawRoundedRectFill(window->sdlRenderer, inner,
                                    window->backgroundColor, innerR);
            }
            FlushRenderBatch(window->sdlRenderer);
        }

        extern void UIWebView_RendererTick(UIWebView*, HWND, int, int, int, int,
                                           const RECT*, const int*, int);
        UIWebView_RendererTick(wv, hwnd,
                               (int)wx, (int)wy, (int)ww, (int)wh,
                               clipBuf, clipRadii, clipCount);
#elif defined(__linux__) && defined(MOCIDA_HAS_WEBKITGTK)
        // Linux WebKitGTK path: paint optional border on the SDL surface
        // (same shape the Windows path uses), then ask the webview for
        // its latest snapshot texture and composite it inside the inset
        // area. The MVP doesn't yet round the corners — the snapshot is
        // drawn as a straight rectangle inside the border.
        UIWebView* wv = (UIWebView*)base;
        if (!el->width || !el->height || !el->visible) return;
        const float wx = el->x, wy = el->y;
        const float ww = *el->width, wh = *el->height;

        extern void UIWebView_GetVisuals(const UIWebView*, float*, float*,
                                         UIColor*, int*);
        extern SDL_Texture* UIWebView_GetSnapshotTexture_Linux(UIWebView*, SDL_Renderer*, int, int);

        float wvRadius = 0.0f, wvBorderW = 0.0f;
        UIColor wvBorderColor = { 0, 0, 0, 0.0f };
        int wvHasBorder = 0;
        UIWebView_GetVisuals(wv, &wvRadius, &wvBorderW, &wvBorderColor, &wvHasBorder);

        SDL_FRect dst = { wx, wy, ww, wh };
        if (wvHasBorder && wvBorderW > 0.0f) {
            UIColor bc = wvBorderColor; bc.a *= op;
            SDL_FRect outer = { wx, wy, ww, wh };
            DrawRoundedRectFill(window->sdlRenderer, outer, bc, wvRadius);
            FlushRenderBatch(window->sdlRenderer);
            dst.x += wvBorderW;
            dst.y += wvBorderW;
            dst.w -= 2.0f * wvBorderW;
            dst.h -= 2.0f * wvBorderW;
        }
        if (dst.w > 0.0f && dst.h > 0.0f) {
            SDL_Texture* wvTex = UIWebView_GetSnapshotTexture_Linux(
                wv, window->sdlRenderer, (int)dst.w, (int)dst.h);
            if (wvTex) {
                SDL_SetTextureBlendMode(wvTex, SDL_BLENDMODE_BLEND);

                // Inner corner radius: when bordered, nest inside the ring
                // exactly like the border path's inner rect; otherwise use
                // the full radius.
                float innerR = wvRadius;
                if (wvHasBorder && wvBorderW > 0.0f) {
                    innerR = wvRadius - wvBorderW;
                    if (innerR < 0.0f) innerR = 0.0f;
                }

                const int rtw = (int)ceilf(dst.w);
                const int rth = (int)ceilf(dst.h);

                // Round only when there is a real radius; otherwise keep the
                // straight, allocation-free fast path.
                SDL_Texture* scratch = (innerR > 0.5f && rtw > 0 && rth > 0)
                    ? SDL_CreateTexture(window->sdlRenderer,
                        SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, rtw, rth)
                    : NULL;

                if (scratch) {
                    // Porter-Duff DST_IN: dst' = dst * src_alpha. Same blend
                    // the UIImage rounded path uses.
                    const SDL_BlendMode kDstInWv = SDL_ComposeCustomBlendMode(
                        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA,
                        SDL_BLENDOPERATION_ADD,
                        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA,
                        SDL_BLENDOPERATION_ADD);

                    SDL_SetTextureBlendMode(scratch, SDL_BLENDMODE_BLEND);
                    SDL_Texture* prevTarget = SDL_GetRenderTarget(window->sdlRenderer);

                    // (1) Snapshot -> scratch at local origin.
                    SDL_SetRenderTarget(window->sdlRenderer, scratch);
                    SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_NONE);
                    SDL_SetRenderDrawColor(window->sdlRenderer, 0, 0, 0, 0);
                    SDL_RenderClear(window->sdlRenderer);
                    SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_BLEND);
                    SDL_FRect localDst = { 0.0f, 0.0f, dst.w, dst.h };
                    SDL_RenderTexture(window->sdlRenderer, wvTex, NULL, &localDst);

                    // (2) White rounded mask -> its own target.
                    SDL_Texture* mask = SDL_CreateTexture(window->sdlRenderer,
                        SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, rtw, rth);
                    if (mask) {
                        SDL_SetTextureBlendMode(mask, SDL_BLENDMODE_BLEND);
                        SDL_SetRenderTarget(window->sdlRenderer, mask);
                        SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_NONE);
                        SDL_SetRenderDrawColor(window->sdlRenderer, 0, 0, 0, 0);
                        SDL_RenderClear(window->sdlRenderer);
                        SDL_SetRenderDrawBlendMode(window->sdlRenderer, SDL_BLENDMODE_BLEND);

                        SDL_FRect maskRect = { 0.0f, 0.0f, dst.w, dst.h };
                        const UIColor white = { 255, 255, 255, 1.0f };
                        DrawRoundedRectFill(window->sdlRenderer, maskRect, white, innerR);

                        // (3) DST_IN: clip scratch to the rounded shape.
                        SDL_SetRenderTarget(window->sdlRenderer, scratch);
                        SDL_SetTextureBlendMode(mask, kDstInWv);
                        SDL_RenderTexture(window->sdlRenderer, mask, NULL, &maskRect);
                        SDL_DestroyTexture(mask);
                    }

                    // (4) Blit rounded scratch to screen with opacity.
                    SDL_SetRenderTarget(window->sdlRenderer, prevTarget);
                    SDL_SetTextureBlendMode(scratch, SDL_BLENDMODE_BLEND);
                    SDL_SetTextureAlphaMod(scratch, (Uint8)(op * 255.0f + 0.5f));
                    SDL_RenderTexture(window->sdlRenderer, scratch, NULL, &dst);
                    SDL_SetTextureAlphaMod(scratch, 255);
                    SDL_DestroyTexture(scratch);
                } else {
                    // No radius (or alloc failed): straight blit, unchanged.
                    SDL_SetTextureAlphaMod(wvTex, (Uint8)(op * 255.0f + 0.5f));
                    SDL_RenderTexture(window->sdlRenderer, wvTex, NULL, &dst);
                    SDL_SetTextureAlphaMod(wvTex, 255);
                }
            }
        }
#elif defined(__APPLE__) && defined(MOCIDA_HAS_WKWEBVIEW)
        // macOS WKWebView path: the webview is a native NSView subview of
        // the SDL window's NSWindow.contentView. It paints itself (and
        // handles its own rounded corners / border via its CALayer), so
        // there is no SDL-side compositing here — we only forward the
        // current bounds + visibility to the Cocoa backend, which
        // positions the subview (flipping Y into Cocoa's bottom-left
        // space). Unlike the early-return paths above we ALSO tick when
        // hidden so the backend can hide the subview.
        UIWebView* wv = (UIWebView*)base;
        if (!el->width || !el->height) return;

        void* nsWindow = NULL;
        if (window->sdlWindow) {
            SDL_PropertiesID props = SDL_GetWindowProperties(window->sdlWindow);
#if defined(MOCIDA_IOS)
            // iOS: the UIKit backend wants the UIWindow.
            nsWindow = SDL_GetPointerProperty(
                props, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL);
#else
            nsWindow = SDL_GetPointerProperty(
                props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
#endif
        }

        extern void UIWebView_RendererTick_Mac(UIWebView*, void*,
                                               int, int, int, int, int);
        UIWebView_RendererTick_Mac(wv, nsWindow,
                                   (int)el->x, (int)el->y,
                                   (int)*el->width, (int)*el->height,
                                   el->visible ? 1 : 0);
#else
        // ---- No webview backend compiled in ----------------------------
        // Reached when the build has no UIWebView implementation for this
        // platform — most commonly Linux without WebKitGTK (the
        // libwebkit2gtk-4.x dev package was absent at configure time, so
        // MOCIDA_HAS_WEBKITGTK is undefined and UIWebView_Create returned a
        // no-op stub). Rather than leave a silently-blank rectangle that
        // looks like a broken page, paint a clear error placeholder so the
        // missing dependency is obvious on screen.
        if (!el->width || !el->height || !el->visible) return;
        {
            const float wx = el->x, wy = el->y;
            const float ww = *el->width, wh = *el->height;
            SDL_FRect box = { wx, wy, ww, wh };

            // Muted slate card so the light message text reads against it.
            UIColor bg = { 30, 41, 59, 1.0f }; bg.a *= op;   // slate-800
            DrawRoundedRectFill(window->sdlRenderer, box, bg, 8.0f);

            const char* msg =
#if defined(__linux__)
                "WebView unavailable\n\n"
                "WebKitGTK was not found at build time.\n"
                "Install the library and rebuild:\n"
                "sudo apt install libwebkit2gtk-4.1-dev";
#else
                "WebView unavailable: no backend for this platform.";
#endif

            // Resolve a usable font path directly so the placeholder works
            // even if UISearchFonts() was never called by the host app.
            const char* fpath = NULL;
#if defined(__APPLE__)
            // /Library/Fonts/Arial.ttf does not exist on modern macOS; Arial
            // ships in /System/Library/Fonts/Supplemental, Helvetica.ttc is
            // always present. Probe candidates so the placeholder still draws.
            static const char* kFontCands[] = {
                "/System/Library/Fonts/Supplemental/Arial.ttf",
                "/Library/Fonts/Arial Unicode.ttf",
                "/System/Library/Fonts/Helvetica.ttc",
                "/System/Library/Fonts/SFNS.ttf",
                NULL
            };
            for (int i = 0; kFontCands[i]; i++) {
                FILE* tf = fopen(kFontCands[i], "rb");
                if (tf) { fclose(tf); fpath = kFontCands[i]; break; }
            }
#else
            static const char* kFontCands[] = {
                "/usr/share/fonts/dejavu/DejaVuSans.ttf",
                "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
                NULL
            };
            for (int i = 0; kFontCands[i]; i++) {
                FILE* tf = fopen(kFontCands[i], "rb");
                if (tf) { fclose(tf); fpath = kFontCands[i]; break; }
            }
#endif
            TTF_Font* font = fpath ? GetFont(fpath, 15.0f) : NULL;
            if (font) {
                SDL_Color sc = { 226, 232, 240,
                                 (Uint8)SDL_clamp((int)(op * 255.0f), 0, 255) };
                int wrapW = (int)(ww - 48.0f);
                if (wrapW < 1) wrapW = 1;
                SDL_Surface* surf = TTF_RenderText_Blended_Wrapped(
                    font, msg, strlen(msg), sc, wrapW);
                if (surf) {
                    SDL_Texture* tex =
                        SDL_CreateTextureFromSurface(window->sdlRenderer, surf);
                    if (tex) {
                        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
                        float tw = 0, th = 0;
                        SDL_GetTextureSize(tex, &tw, &th);
                        SDL_FRect tdst = { wx + (ww - tw) * 0.5f,
                                           wy + (wh - th) * 0.5f, tw, th };
                        SDL_RenderTexture(window->sdlRenderer, tex, NULL, &tdst);
                        SDL_DestroyTexture(tex);
                    }
                    SDL_DestroySurface(surf);
                }
            }
        }
#endif
}

static void RenderWidget_FileDrop(UIWindow* window, UIWidget* el,
                                UIWidgetBase* base, float op) {

        UIFileDrop* fd = (UIFileDrop*)base;
        if (!el->width || !el->height) return;
        SDL_FRect bounds = { el->x, el->y, *el->width, *el->height };

        const UIColor bgRaw  = fd->dragOver ? fd->activeBgColor     : fd->bgColor;
        const UIColor brRaw  = fd->dragOver ? fd->activeBorderColor : fd->borderColor;
        UIColor bg = bgRaw; bg.a *= op;
        UIColor br = brRaw; br.a *= op;
        DrawRoundedRectWithBorder(window->sdlRenderer, bounds, bg, fd->radius,
                                  (int)fd->borderWidth, br);

        // Centred prompt text.
        if (fd->prompt && *fd->prompt && fd->fontFamily) {
            TTF_Font* font = GetFont(fd->fontFamily, fd->fontSize);
            if (font) {
                TTF_SetFontStyle(font, TTF_STYLE_NORMAL);
                UIColor tc = fd->textColor; tc.a *= op;
                SDL_Color sc = {(Uint8)tc.r,(Uint8)tc.g,(Uint8)tc.b,
                                (Uint8)SDL_clamp((int)(tc.a*255.0f),0,255)};
                const size_t len = strlen(fd->prompt);
                SDL_Surface* surf = TTF_RenderText_Blended(font, fd->prompt, len, sc);
                if (surf) {
                    SDL_Texture* tex = SDL_CreateTextureFromSurface(window->sdlRenderer, surf);
                    if (tex) {
                        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
                        float tw=0, th=0;
                        SDL_GetTextureSize(tex, &tw, &th);
                        SDL_FRect dst = { bounds.x + (bounds.w - tw) * 0.5f,
                                          bounds.y + (bounds.h - th) * 0.5f,
                                          tw, th };
                        SDL_RenderTexture(window->sdlRenderer, tex, NULL, &dst);
                        SDL_DestroyTexture(tex);
                    }
                    SDL_DestroySurface(surf);
                }
            }
        }
}

static void RenderWidget_Checkbox(UIWindow* window, UIWidget* el,
                                UIWidgetBase* base, float op) {

        UICheckbox* c = (UICheckbox*)base;
        if (!el->width || !el->height) return;
        SDL_FRect bounds = { el->x, el->y, *el->width, *el->height };

        UIColor boxC    = c->boxColor;    boxC.a    *= op;
        UIColor borderC = c->borderColor; borderC.a *= op;
        UIColor checkC  = c->checkColor;  checkC.a  *= op;

        // Box
        DrawRoundedRectWithBorder(window->sdlRenderer, bounds,
                                  boxC, c->radius,
                                  (int)c->borderWidth, borderC);

        // Ease _phase toward the checked target.
        const float target = c->checked ? 1.0f : 0.0f;
        if (c->animMs > 0) {
            const float k = 1.0f - powf(0.3f, 1.0f / (float)c->animMs);
            c->_phase += (target - c->_phase) * k * 16.0f;
            if ((target == 1.0f && c->_phase > 0.999f) ||
                (target == 0.0f && c->_phase < 0.001f)) {
                c->_phase = target;
            }
        } else {
            c->_phase = target;
        }

        // Draw the check mark as two line segments forming a "✓". The
        // stroke length is revealed proportionally to _phase, so it
        // looks like the symbol is being drawn from start to end (and
        // erased back when unchecking).
        if (c->_phase > 0.001f) {
            const float ph = c->_phase;
            // Three control points in normalised coordinates within
            // the checkbox bounds. Tuned to feel balanced regardless
            // of box size.
            const float P1x = 0.22f, P1y = 0.52f;  // upper-left start
            const float P2x = 0.43f, P2y = 0.73f;  // lower-middle corner
            const float P3x = 0.80f, P3y = 0.30f;  // upper-right end

            const float ax = bounds.x + bounds.w * P1x;
            const float ay = bounds.y + bounds.h * P1y;
            const float bx = bounds.x + bounds.w * P2x;
            const float by = bounds.y + bounds.h * P2y;
            const float cx = bounds.x + bounds.w * P3x;
            const float cy = bounds.y + bounds.h * P3y;

            const float L1 = sqrtf((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
            const float L2 = sqrtf((cx - bx) * (cx - bx) + (cy - by) * (cy - by));
            const float total = L1 + L2;
            const float visible = total * ph;

            // Stroke thickness scales with box size so the mark stays
            // readable at any scale.
            const float minDim = SDL_min(bounds.w, bounds.h);
            const float thick = SDL_max(1.2f, minDim * 0.12f);
            const float radius = thick * 0.5f;
            const float step = SDL_max(0.5f, radius * 0.6f);

            // Helper to walk a segment and stamp circles at `step`
            // intervals up to `endDist`. Returns the distance covered.
            // Inlined as two loops to keep the code C and dependency-free.
            float drawn = 0.0f;

            // Segment 1: A -> B
            {
                const float maxD = SDL_min(visible, L1);
                if (maxD > 0.0f && L1 > 0.0f) {
                    const float dx = (bx - ax) / L1;
                    const float dy = (by - ay) / L1;
                    for (float d = 0.0f; d <= maxD; d += step) {
                        const float px = ax + dx * d;
                        const float py = ay + dy * d;
                        DrawCircle(window->sdlRenderer, px, py, radius, checkC);
                    }
                    // Cap at the end of the visible portion so the
                    // stroke doesn't have a gap.
                    const float ex = ax + dx * maxD;
                    const float ey = ay + dy * maxD;
                    DrawCircle(window->sdlRenderer, ex, ey, radius, checkC);
                    drawn = maxD;
                }
            }

            // Segment 2: B -> C, only after segment 1 is fully drawn.
            if (visible > L1 && L2 > 0.0f) {
                const float remaining = SDL_min(visible - L1, L2);
                const float dx = (cx - bx) / L2;
                const float dy = (cy - by) / L2;
                for (float d = 0.0f; d <= remaining; d += step) {
                    const float px = bx + dx * d;
                    const float py = by + dy * d;
                    DrawCircle(window->sdlRenderer, px, py, radius, checkC);
                }
                const float ex = bx + dx * remaining;
                const float ey = by + dy * remaining;
                DrawCircle(window->sdlRenderer, ex, ey, radius, checkC);
                (void)drawn;
            }
        }
}

static void RenderWidget_Slider(UIWindow* window, UIWidget* el,
                                UIWidgetBase* base, float op) {

        UISlider* s = (UISlider*)base;
        if (!el->width || !el->height) return;

        const float W = *el->width;
        const float H = *el->height;
        const float trackY = el->y + (H - s->trackHeight) * 0.5f;
        SDL_FRect track = { el->x, trackY, W, s->trackHeight };

        UIColor trackC = s->trackColor; trackC.a *= op;
        UIColor fillC  = s->fillColor;  fillC.a  *= op;
        UIColor knobC  = s->knobColor;  knobC.a  *= op;

        DrawRoundedRectFill(window->sdlRenderer, track, trackC, s->trackHeight * 0.5f);

        const float range = (s->maxValue - s->minValue);
        const float ratio = (range > 0.0f) ? (s->value - s->minValue) / range : 0.0f;
        SDL_FRect filled = { el->x, trackY, W * ratio, s->trackHeight };
        if (filled.w > 0.0f) {
            DrawRoundedRectFill(window->sdlRenderer, filled, fillC, s->trackHeight * 0.5f);
        }

        // Knob: filled circle centred at the value position.
        const float knobX = el->x + W * ratio;
        const float knobY = el->y + H * 0.5f;
        DrawCircle(window->sdlRenderer, knobX, knobY, s->knobRadius, knobC);
}

static void RenderWidget_ProgressBar(UIWindow* window, UIWidget* el,
                                UIWidgetBase* base, float op) {

        UIProgressBar* p = (UIProgressBar*)base;
        if (!el->width || !el->height) return;
        SDL_FRect bounds = { el->x, el->y, *el->width, *el->height };

        UIColor trackC = p->trackColor; trackC.a *= op;
        UIColor fillC  = p->fillColor;  fillC.a  *= op;

        DrawRoundedRectFill(window->sdlRenderer, bounds, trackC, p->radius);

        if (p->indeterminate) {
            // Advance phase based on wall-clock so the strip travels
            // independently of frame timing.
            const float t = (float)((SDL_GetTicks() % 1500ULL)) / 1500.0f;
            const float segW = bounds.w * 0.3f;
            const float x    = bounds.x + (bounds.w + segW) * t - segW;
            const float clampedX = (x < bounds.x) ? bounds.x : x;
            const float clampedW = (x + segW > bounds.x + bounds.w)
                                   ? (bounds.x + bounds.w - clampedX)
                                   : (segW - (clampedX - x));
            if (clampedW > 0.0f) {
                SDL_FRect bar = { clampedX, bounds.y, clampedW, bounds.h };
                DrawRoundedRectFill(window->sdlRenderer, bar, fillC, p->radius);
            }
            (void)p->_phase;
        } else {
            float v = p->value;
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            SDL_FRect bar = { bounds.x, bounds.y, bounds.w * v, bounds.h };
            if (bar.w > 0.0f) {
                DrawRoundedRectFill(window->sdlRenderer, bar, fillC, p->radius);
            }
        }
}

static void RenderWidget_TextField(UIWindow* window, UIWidget* el,
                                UIWidgetBase* base, float op) {

        UITextField* tf = (UITextField*)base;
        if (!el->width || !el->height) return;
        SDL_FRect bounds = { el->x, el->y, *el->width, *el->height };

        UIColor bg     = tf->bgColor;       bg.a     *= op;
        UIColor border = (tf->focused ? tf->borderColorFocused : tf->borderColor);
        border.a *= op;

        DrawRoundedRectWithBorder(window->sdlRenderer, bounds,
                                  bg, tf->radius,
                                  (int)tf->borderWidth, border);

        // Decide which string to render and which colour.
        const int   showPlaceholder = (tf->textLen == 0 && tf->placeholder);
        const char* renderStr = showPlaceholder ? tf->placeholder : tf->text;
        int         renderLen = showPlaceholder
                              ? (int)strlen(tf->placeholder)
                              : tf->textLen;

        // Password mask: replace each glyph with a bullet (U+2022 in UTF-8
        // is e2 80 a2). For simplicity we use ASCII '*' here.
        char  passBuf[256];
        int   passUsed = 0;
        if (!showPlaceholder && tf->passwordMask && renderLen > 0) {
            int n = renderLen;
            if (n > (int)sizeof(passBuf) - 1) n = sizeof(passBuf) - 1;
            for (int i = 0; i < n; i++) passBuf[i] = '*';
            passBuf[n] = '\0';
            renderStr = passBuf;
            renderLen = n;
            passUsed  = 1;
        }
        (void)passUsed;

        // GetFont(NULL, size) returns the default font, so a field with no
        // explicit fontFamily still renders its text (matches UIText, which
        // calls GetFont the same way). Guarding on fontFamily here was why a
        // field with no `font:` showed nothing despite holding text.
        TTF_Font* font = (tf->fontSize > 0.0f)
            ? GetFont(tf->fontFamily, tf->fontSize) : NULL;
        if (font) {
            TTF_SetFontStyle(font, (TTF_FontStyleFlags)tf->fontStyle);
        }

        // Lazily regenerate the text texture when the content changed.
        // `textChanged` is reused below to gate the (expensive) per-glyph
        // caret-offset measurement so it doesn't run every frame.
        const int textChanged = (!tf->__SDL_textTexture ||
                                 tf->__cachedTextLen != renderLen);
        if (font && textChanged) {
            if (tf->__SDL_textTexture) {
                SDL_DestroyTexture(tf->__SDL_textTexture);
                tf->__SDL_textTexture = NULL;
            }
            const UIColor c = showPlaceholder ? tf->placeholderColor : tf->textColor;
            SDL_Color sc = { (Uint8)c.r, (Uint8)c.g, (Uint8)c.b,
                             (Uint8)SDL_clamp((int)(c.a * 255.0f), 0, 255) };
            if (renderLen > 0) {
                SDL_Surface* surf = TTF_RenderText_Blended(font, renderStr,
                                                          (size_t)renderLen, sc);
                if (surf) {
                    tf->__SDL_textTexture = SDL_CreateTextureFromSurface(
                        window->sdlRenderer, surf);
                    SDL_DestroySurface(surf);
                    if (tf->__SDL_textTexture) {
                        SDL_SetTextureScaleMode(tf->__SDL_textTexture, SDL_SCALEMODE_LINEAR);
                    }
                }
            }
            tf->__cachedTextLen = renderLen;
        }

        // Rebuild __charOffsets so the dispatcher can map mouse x to a
        // caret position. We track offsets for the *actual* edited text
        // (tf->text), not for the placeholder. When the placeholder is
        // visible there is nothing to click into anyway.
        if (font && !showPlaceholder) {
            const int need = tf->textLen + 1;
            const int offsetsStale = (tf->__charOffsetsLen != need) || textChanged;
            if (tf->__charOffsetsLen != need) {
                int* p = (int*)realloc(tf->__charOffsets, (size_t)need * sizeof(int));
                if (p) {
                    tf->__charOffsets    = p;
                    tf->__charOffsetsLen = need;
                }
            }
            // Only re-measure per-glyph offsets when the text actually
            // changed — TTF_GetStringSize per character every frame is O(n)
            // shaping work that dominated focused-field CPU otherwise.
            if (offsetsStale && tf->__charOffsets && tf->__charOffsetsLen == need) {
                const char* measureStr = renderStr; // matches glyph rendering
                tf->__charOffsets[0] = 0;
                for (int i = 1; i <= tf->textLen; i++) {
                    int w = 0, h = 0;
                    TTF_GetStringSize(font, measureStr, (size_t)i, &w, &h);
                    tf->__charOffsets[i] = w;
                }
            }
        } else if (showPlaceholder) {
            // Discard stale offsets so a click on an empty field can't
            // land the caret past 0. The dispatcher already early-outs
            // when textLen <= 0.
            tf->__charOffsetsLen = 0;
        }

        // Clip drawing of glyphs + caret to the inside of the field.
        SDL_Rect clip = {
            (int)(bounds.x + tf->paddingLeft),
            (int)(bounds.y),
            (int)(bounds.w - (tf->paddingLeft + tf->paddingRight)),
            (int)(bounds.h)
        };
        SDL_SetRenderClipRect(window->sdlRenderer, &clip);

        // Selection highlight - drawn behind the glyphs so the text
        // colour stays untouched.
        if (!showPlaceholder && tf->selAnchor >= 0 &&
            tf->selAnchor != tf->caretPos &&
            tf->__charOffsets && tf->__charOffsetsLen == tf->textLen + 1) {
            int s = tf->selAnchor, e = tf->caretPos;
            if (s > e) { int t = s; s = e; e = t; }
            if (s < 0) s = 0;
            if (e > tf->textLen) e = tf->textLen;
            const float sx = (float)tf->__charOffsets[s];
            const float ex = (float)tf->__charOffsets[e];
            const float caretH = tf->fontSize * 1.1f;
            SDL_FRect selRect = {
                bounds.x + tf->paddingLeft + sx,
                bounds.y + (bounds.h - caretH) * 0.5f,
                ex - sx,
                caretH
            };
            UIColor sc2 = tf->selectionColor; sc2.a *= op;
            DrawRoundedRectFill(window->sdlRenderer, selRect, sc2, 2.0f);
        }

        if (tf->__SDL_textTexture && renderLen > 0) {
            float tw = 0.0f, th = 0.0f;
            SDL_GetTextureSize(tf->__SDL_textTexture, &tw, &th);
            const float tx = bounds.x + tf->paddingLeft;
            const float ty = bounds.y + (bounds.h - th) * 0.5f;
            SDL_FRect dst = { tx, ty, tw, th };

            /* Placeholder pulse: only when the field is empty, unfocused,
             * and SetPlaceholderAnimated(true) was called. Sine on a
             * 2.5 s period, gentle band 55% .. 100%. */
            float alpha = op;
            if (showPlaceholder && tf->placeholderAnimated && !tf->focused) {
                const double t = (double)SDL_GetTicks() * 0.001;
                const double s = (SDL_sin(t * 2.5132741) + 1.0) * 0.5; /* 2pi/2.5 */
                alpha = op * (float)(0.55 + 0.45 * s);
            }
            SDL_SetTextureAlphaMod(tf->__SDL_textTexture, (Uint8)(alpha * 255.0f + 0.5f));
            SDL_RenderTexture(window->sdlRenderer, tf->__SDL_textTexture, NULL, &dst);
            SDL_SetTextureAlphaMod(tf->__SDL_textTexture, 255);
        }

        // Caret. Position derives from substring width up to the caret.
        if (tf->focused && font) {
            /* Caret blink: cadence configurable via SetCaretBlinkRate.
             * blinkMs <= 0 keeps it solid (useful for screenshots). */
            const int blinkMs = tf->caretBlinkMs;
            int visible;
            if (blinkMs <= 0) {
                visible = 1;
            } else {
                const Uint64 t = SDL_GetTicks();
                visible = (int)(((t / (Uint64)blinkMs) & 1ULL) == 0);
            }
            if (visible) {
                int caretBytes = tf->caretPos;
                if (caretBytes > tf->textLen) caretBytes = tf->textLen;

                int w = 0, h = 0;
                const char* measureStr = renderStr;
                int          measureLen = caretBytes;
                if (tf->passwordMask) {
                    // For password fields, measure '*' repeated to caretBytes.
                    measureStr = passBuf;
                    if (measureLen > (int)sizeof(passBuf) - 1)
                        measureLen = sizeof(passBuf) - 1;
                }
                if (measureLen > 0) {
                    TTF_GetStringSize(font, measureStr, (size_t)measureLen, &w, &h);
                }
                const float caretX = bounds.x + tf->paddingLeft + (float)w;
                const float caretH = tf->fontSize * 1.1f;
                const float caretY = bounds.y + (bounds.h - caretH) * 0.5f;
                UIColor cc = tf->caretColor; cc.a *= op;
                SDL_FRect caretRect = { caretX, caretY, 1.5f, caretH };
                DrawRoundedRectFill(window->sdlRenderer, caretRect, cc, 0.0f);
            }
        }

        SDL_SetRenderClipRect(window->sdlRenderer, NULL);
}

static void RenderWidget_TextArea(UIWindow* window, UIWidget* el,
                                UIWidgetBase* base, float op) {

        UITextArea* ta = (UITextArea*)base;
        if (!el->width || !el->height) return;
        SDL_FRect bounds = { el->x, el->y, *el->width, *el->height };

        UIColor bg2     = ta->bgColor;     bg2.a     *= op;
        UIColor border2 = ta->focused ? ta->borderColorFocused : ta->borderColor;
        border2.a *= op;
        DrawRoundedRectWithBorder(window->sdlRenderer, bounds,
                                  bg2, ta->radius,
                                  (int)ta->borderWidth, border2);

        TTF_Font* font = (ta->fontSize > 0.0f)
            ? GetFont(ta->fontFamily, ta->fontSize) : NULL;
        if (!font) return;
        TTF_SetFontStyle(font, (TTF_FontStyleFlags)ta->fontStyle);

        const int showPlaceholder = (ta->textLen == 0 && ta->placeholder);
        const float lineH = ta->fontSize * ta->lineSpacing;

        // Available pixel width inside the padding box. When wrap is
        // enabled and this value changes (the user resized the widget),
        // we have to rebuild the line cache.
        int wrapW = -1;
        if (ta->wrapMode != UI_WRAP_NONE) {
            wrapW = (int)(bounds.w - ta->paddingLeft - ta->paddingRight);
            if (wrapW < 1) wrapW = -1;
        }

        const int needRebuild =
            ta->__cachedTextLen  != ta->textLen ||
            ta->__cachedWrapMode != (int)ta->wrapMode ||
            ta->__cachedWrapW    != wrapW;

        // Rebuild the per-line cache when the text/wrap config changed.
        // Each visual line gets its own glyph texture + char-offset array.
        if (needRebuild) {
            // Reuse InvalidateLineCache: it frees and zeroes.
            if (ta->lineTextures) {
                for (int i = 0; i < ta->linesLen; i++) {
                    if (ta->lineTextures[i]) SDL_DestroyTexture(ta->lineTextures[i]);
                }
            }
            if (ta->lineCharOffsets) {
                for (int i = 0; i < ta->linesLen; i++) {
                    free(ta->lineCharOffsets[i]);
                }
            }
            free(ta->lineTextures);       ta->lineTextures = NULL;
            free(ta->lineStarts);         ta->lineStarts = NULL;
            free(ta->lineLengths);        ta->lineLengths = NULL;
            free(ta->lineCharOffsets);    ta->lineCharOffsets = NULL;
            free(ta->lineCharOffsetsLen); ta->lineCharOffsetsLen = NULL;
            free(ta->lineIsSoft);         ta->lineIsSoft = NULL;
            ta->linesLen = 0;

            // Initial capacity: hard line count, with extra room when
            // wrap is on (each logical line may produce several visual
            // segments). The arrays grow on demand inside the loop.
            int cap = 1;
            for (int i = 0; i < ta->textLen; i++) if (ta->text[i] == '\n') cap++;
            if (wrapW > 0) { cap = cap * 2 + 4; }
            if (cap < 1) cap = 1;

            ta->linesCap            = cap;
            ta->lineTextures        = (SDL_Texture**)calloc(cap, sizeof(SDL_Texture*));
            ta->lineStarts          = (int*)calloc(cap, sizeof(int));
            ta->lineLengths         = (int*)calloc(cap, sizeof(int));
            ta->lineCharOffsets     = (int**)calloc(cap, sizeof(int*));
            ta->lineCharOffsetsLen  = (int*)calloc(cap, sizeof(int));
            ta->lineIsSoft          = (int*)calloc(cap, sizeof(int));

            const UIColor c = ta->textColor;
            const SDL_Color sc = { (Uint8)c.r, (Uint8)c.g, (Uint8)c.b,
                                   (Uint8)SDL_clamp((int)(c.a * 255.0f), 0, 255) };

            // Iterate over logical lines (separated by '\n'). Inside
            // each, either emit a single segment (wrap off) or break
            // it into visual segments that fit in wrapW.
            int logicalStart = 0;
            for (int i = 0; i <= ta->textLen; i++) {
                if (i < ta->textLen && ta->text[i] != '\n') continue;
                const int lineEnd = i;

                int segStart    = logicalStart;
                int firstInLine = 1;

                do {
                    int segEnd;
                    if (wrapW <= 0 || lineEnd - segStart == 0) {
                        segEnd = lineEnd;
                    } else {
                        // Does the whole remainder fit on one line?
                        int w = 0, h = 0;
                        TTF_GetStringSize(font, ta->text + segStart,
                                          (size_t)(lineEnd - segStart), &w, &h);
                        if (w <= wrapW) {
                            segEnd = lineEnd;
                        } else {
                            // Binary-search the longest prefix that fits.
                            int lo = 1, hi = lineEnd - segStart, fit = 1;
                            while (lo <= hi) {
                                int mid = (lo + hi) / 2;
                                int ww = 0, hh = 0;
                                TTF_GetStringSize(font, ta->text + segStart,
                                                  (size_t)mid, &ww, &hh);
                                if (ww <= wrapW) { fit = mid; lo = mid + 1; }
                                else              { hi = mid - 1; }
                            }
                            int cut = fit;
                            if (ta->wrapMode == UI_WRAP_WORD) {
                                // Back up to just after the last
                                // whitespace inside the fitting prefix
                                // so trailing spaces stay with this
                                // line. Falls back to char-wrap when
                                // no whitespace was found (a single
                                // very long word).
                                int spAfter = -1;
                                for (int k = cut; k > 0; k--) {
                                    char ch = ta->text[segStart + k - 1];
                                    if (ch == ' ' || ch == '\t') { spAfter = k; break; }
                                }
                                if (spAfter > 0) cut = spAfter;
                            }
                            if (cut <= 0) cut = 1;
                            segEnd = segStart + cut;
                        }
                    }

                    // Grow the arrays if we ran out of capacity.
                    if (ta->linesLen >= ta->linesCap) {
                        int newCap = ta->linesCap * 2;
                        if (newCap < ta->linesLen + 1) newCap = ta->linesLen + 8;
                        ta->lineTextures        = (SDL_Texture**)realloc(ta->lineTextures,        newCap * sizeof(SDL_Texture*));
                        ta->lineStarts          = (int*)         realloc(ta->lineStarts,          newCap * sizeof(int));
                        ta->lineLengths         = (int*)         realloc(ta->lineLengths,         newCap * sizeof(int));
                        ta->lineCharOffsets     = (int**)        realloc(ta->lineCharOffsets,     newCap * sizeof(int*));
                        ta->lineCharOffsetsLen  = (int*)         realloc(ta->lineCharOffsetsLen,  newCap * sizeof(int));
                        ta->lineIsSoft          = (int*)         realloc(ta->lineIsSoft,          newCap * sizeof(int));
                        for (int z = ta->linesCap; z < newCap; z++) {
                            ta->lineTextures[z]       = NULL;
                            ta->lineStarts[z]         = 0;
                            ta->lineLengths[z]        = 0;
                            ta->lineCharOffsets[z]    = NULL;
                            ta->lineCharOffsetsLen[z] = 0;
                            ta->lineIsSoft[z]         = 0;
                        }
                        ta->linesCap = newCap;
                    }

                    const int li     = ta->linesLen;
                    const int segLen = segEnd - segStart;
                    ta->lineStarts[li]  = segStart;
                    ta->lineLengths[li] = segLen;
                    ta->lineIsSoft[li]  = firstInLine ? 0 : 1;

                    if (segLen > 0) {
                        SDL_Surface* surf = TTF_RenderText_Blended(
                            font, ta->text + segStart, (size_t)segLen, sc);
                        if (surf) {
                            ta->lineTextures[li] = SDL_CreateTextureFromSurface(
                                window->sdlRenderer, surf);
                            SDL_DestroySurface(surf);
                            if (ta->lineTextures[li]) {
                                SDL_SetTextureScaleMode(ta->lineTextures[li], SDL_SCALEMODE_LINEAR);
                            }
                        }
                    } else {
                        ta->lineTextures[li] = NULL;
                    }

                    const int need = segLen + 1;
                    ta->lineCharOffsetsLen[li] = need;
                    ta->lineCharOffsets[li] = (int*)calloc(need, sizeof(int));
                    if (ta->lineCharOffsets[li]) {
                        ta->lineCharOffsets[li][0] = 0;
                        for (int k = 1; k < need; k++) {
                            int w = 0, h = 0;
                            TTF_GetStringSize(font, ta->text + segStart, (size_t)k, &w, &h);
                            ta->lineCharOffsets[li][k] = w;
                        }
                    }

                    ta->linesLen++;
                    firstInLine = 0;
                    segStart = segEnd;
                } while (segStart < lineEnd);

                logicalStart = lineEnd + 1; // skip the '\n'
            }

            ta->__cachedTextLen  = ta->textLen;
            ta->__cachedWrapMode = (int)ta->wrapMode;
            ta->__cachedWrapW    = wrapW;
        }

        // Auto-scroll so the caret stays inside the viewport.
        {
            int pos = ta->caretPos;
            if (pos < 0) pos = 0;
            if (pos > ta->textLen) pos = ta->textLen;
            int lo = 0, hi = ta->linesLen - 1;
            while (lo < hi) {
                int mid = (lo + hi + 1) / 2;
                if (ta->lineStarts[mid] <= pos) lo = mid; else hi = mid - 1;
            }
            const int caretLine = lo;

            const float viewH = bounds.h - (ta->paddingTop + ta->paddingBottom);
            const float caretY = caretLine * lineH;
            if (caretY < ta->scrollY) ta->scrollY = caretY;
            if (caretY + lineH > ta->scrollY + viewH) {
                ta->scrollY = caretY + lineH - viewH;
            }
            if (ta->scrollY < 0.0f) ta->scrollY = 0.0f;
        }

        // Clip the inside of the field so glyphs / caret never bleed
        // over the padding/border.
        SDL_Rect clipR = {
            (int)(bounds.x + ta->paddingLeft),
            (int)(bounds.y + ta->paddingTop),
            (int)(bounds.w - (ta->paddingLeft + ta->paddingRight)),
            (int)(bounds.h - (ta->paddingTop + ta->paddingBottom))
        };
        SDL_SetRenderClipRect(window->sdlRenderer, &clipR);

        // Selection highlight - emit one rect per affected line.
        if (!showPlaceholder && ta->selAnchor >= 0 &&
            ta->selAnchor != ta->caretPos && ta->linesLen > 0) {
            int s, e;
            // (inline SelectionRange)
            s = ta->selAnchor; e = ta->caretPos;
            if (s > e) { int t = s; s = e; e = t; }
            if (s < 0) s = 0;
            if (e > ta->textLen) e = ta->textLen;

            int sLine = 0, eLine = 0;
            { int lo = 0, hi = ta->linesLen - 1;
              while (lo < hi) {
                int mid = (lo + hi + 1) / 2;
                if (ta->lineStarts[mid] <= s) lo = mid; else hi = mid - 1;
              } sLine = lo; }
            { int lo = 0, hi = ta->linesLen - 1;
              while (lo < hi) {
                int mid = (lo + hi + 1) / 2;
                if (ta->lineStarts[mid] <= e) lo = mid; else hi = mid - 1;
              } eLine = lo; }

            UIColor selC = ta->selectionColor; selC.a *= op;
            for (int li = sLine; li <= eLine; li++) {
                if (!ta->lineCharOffsets[li]) continue;
                int colStart = (li == sLine) ? s - ta->lineStarts[li] : 0;
                int colEnd   = (li == eLine) ? e - ta->lineStarts[li]
                                             : ta->lineLengths[li];
                if (colStart < 0) colStart = 0;
                if (colEnd > ta->lineLengths[li]) colEnd = ta->lineLengths[li];
                const float x1 = (float)ta->lineCharOffsets[li][colStart];
                const float x2 = (float)ta->lineCharOffsets[li][colEnd];
                // If selection wraps across a hard newline at end of
                // line, add a small visual tail so the highlight reads.
                // Soft-wrapped breaks (visual wrap, same logical line)
                // skip the tail since there's no real '\n' to mark.
                const int isHardBreak = (li < eLine) &&
                    (li + 1 >= ta->linesLen || !ta->lineIsSoft[li + 1]);
                const float tail = isHardBreak ? ta->fontSize * 0.4f : 0.0f;
                SDL_FRect r = {
                    bounds.x + ta->paddingLeft + x1,
                    bounds.y + ta->paddingTop + li * lineH - ta->scrollY,
                    (x2 - x1) + tail,
                    lineH
                };
                DrawRoundedRectFill(window->sdlRenderer, r, selC, 2.0f);
            }
        }

        // Glyphs.
        if (showPlaceholder) {
            const UIColor c = ta->placeholderColor;
            SDL_Color sc = { (Uint8)c.r, (Uint8)c.g, (Uint8)c.b,
                             (Uint8)SDL_clamp((int)(c.a * 255.0f), 0, 255) };
            const int phLen = (int)strlen(ta->placeholder);
            SDL_Surface* surf = TTF_RenderText_Blended(font, ta->placeholder,
                                                      (size_t)phLen, sc);
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(window->sdlRenderer, surf);
                SDL_DestroySurface(surf);
                if (tex) {
                    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
                    float tw, th;
                    SDL_GetTextureSize(tex, &tw, &th);
                    SDL_FRect dst = {
                        bounds.x + ta->paddingLeft,
                        bounds.y + ta->paddingTop,
                        tw, th
                    };
                    /* Same pulse logic as the textfield: when the area
                     * is empty + unfocused + SetPlaceholderAnimated(1),
                     * breathe the alpha on a slow sine. */
                    float alpha = op;
                    if (ta->placeholderAnimated && !ta->focused) {
                        const double tt = (double)SDL_GetTicks() * 0.001;
                        const double s = (SDL_sin(tt * 2.5132741) + 1.0) * 0.5;
                        alpha = op * (float)(0.55 + 0.45 * s);
                    }
                    SDL_SetTextureAlphaMod(tex, (Uint8)(alpha * 255.0f + 0.5f));
                    SDL_RenderTexture(window->sdlRenderer, tex, NULL, &dst);
                    SDL_DestroyTexture(tex);
                }
            }
        } else {
            for (int li = 0; li < ta->linesLen; li++) {
                if (!ta->lineTextures[li]) continue;
                float tw, th;
                SDL_GetTextureSize(ta->lineTextures[li], &tw, &th);
                SDL_FRect dst = {
                    bounds.x + ta->paddingLeft,
                    bounds.y + ta->paddingTop + li * lineH - ta->scrollY,
                    tw, th
                };
                SDL_SetTextureAlphaMod(ta->lineTextures[li], (Uint8)(op * 255.0f + 0.5f));
                SDL_RenderTexture(window->sdlRenderer, ta->lineTextures[li], NULL, &dst);
                SDL_SetTextureAlphaMod(ta->lineTextures[li], 255);
            }
        }

        // Caret. Half-period configurable via UITextArea_SetCaretBlinkRate;
        // 0 keeps it solid.
        if (ta->focused && ta->linesLen > 0) {
            const int blinkMs = ta->caretBlinkMs;
            int visible;
            if (blinkMs <= 0) {
                visible = 1;
            } else {
                const Uint64 t = SDL_GetTicks();
                visible = (int)(((t / (Uint64)blinkMs) & 1ULL) == 0);
            }
            if (visible) {
                int caretLine = 0;
                int pos = ta->caretPos;
                if (pos < 0) pos = 0;
                if (pos > ta->textLen) pos = ta->textLen;
                int lo = 0, hi = ta->linesLen - 1;
                while (lo < hi) {
                    int mid = (lo + hi + 1) / 2;
                    if (ta->lineStarts[mid] <= pos) lo = mid; else hi = mid - 1;
                }
                caretLine = lo;
                const int col = pos - ta->lineStarts[caretLine];
                float cx = 0.0f;
                if (ta->lineCharOffsets[caretLine] &&
                    col < ta->lineCharOffsetsLen[caretLine]) {
                    cx = (float)ta->lineCharOffsets[caretLine][col];
                }
                const float caretH = ta->fontSize * 1.1f;
                UIColor cc = ta->caretColor; cc.a *= op;
                SDL_FRect caretRect = {
                    bounds.x + ta->paddingLeft + cx,
                    bounds.y + ta->paddingTop + caretLine * lineH - ta->scrollY
                        + (lineH - caretH) * 0.5f,
                    1.5f, caretH
                };
                DrawRoundedRectFill(window->sdlRenderer, caretRect, cc, 0.0f);
            }
        }

        SDL_SetRenderClipRect(window->sdlRenderer, NULL);
}

static void RenderWidget_Spinner(UIWindow* window, UIWidget* el,
                                UIWidgetBase* base, float op) {

        UISpinner* sp = (UISpinner*)base;
        if (!el->width || !el->height) return;

        UIColor c = sp->color; c.a *= op;

        // Drive rotation from wall-clock with the user-configurable
        // speed (rad/s). Default = 2*pi (one revolution per second).
        const float speed = (sp->speed != 0.0f) ? sp->speed : 6.28318530718f;
        const float phase = (float)(SDL_GetTicks() * 0.001) * speed;
        sp->_phase = phase;

        const float cx = el->x + *el->width * 0.5f;
        const float cy = el->y + *el->height * 0.5f;
        const float r  = sp->radius;

        // 12 dots positioned around the ring with descending alpha.
        const int N = 12;
        for (int i = 0; i < N; i++) {
            const float a = phase + i * (6.28318530718f / N);
            const float dotX = cx + cosf(a) * r;
            const float dotY = cy + sinf(a) * r;
            const float fade = (float)i / (float)N;
            UIColor dc = c;
            dc.a = c.a * fade;
            DrawCircle(window->sdlRenderer, dotX, dotY, sp->thickness, dc);
        }
}

static void RenderWidget_Radio(UIWindow* window, UIWidget* el,
                                UIWidgetBase* base, float op) {

        UIRadioButton* r = (UIRadioButton*)base;
        if (!el->width || !el->height) return;

        const float w = *el->width;
        const float h = *el->height;
        const float cx = el->x + w * 0.5f;
        const float cy = el->y + h * 0.5f;
        const float outerR = SDL_min(w, h) * 0.5f;

        UIColor boxC    = r->boxColor;    boxC.a    *= op;
        UIColor borderC = r->borderColor; borderC.a *= op;
        UIColor dotC    = r->dotColor;    dotC.a    *= op;

        // Outer disc: border ring first, then inner fill.
        if (r->borderWidth > 0.0f) {
            DrawCircle(window->sdlRenderer, cx, cy, outerR, borderC);
            const float innerR = SDL_max(0.0f, outerR - r->borderWidth);
            if (innerR > 0.0f) {
                DrawCircle(window->sdlRenderer, cx, cy, innerR, boxC);
            }
        } else {
            DrawCircle(window->sdlRenderer, cx, cy, outerR, boxC);
        }

        // Ease _phase toward the selected target.
        const float target = r->selected ? 1.0f : 0.0f;
        if (r->animMs > 0) {
            const float k = 1.0f - powf(0.3f, 1.0f / (float)r->animMs);
            r->_phase += (target - r->_phase) * k * 16.0f;
            if ((target == 1.0f && r->_phase > 0.999f) ||
                (target == 0.0f && r->_phase < 0.001f)) {
                r->_phase = target;
            }
        } else {
            r->_phase = target;
        }

        // Inner dot grows from the centre with the phase. Alpha also
        // fades so the appearance/disappearance feels softer.
        if (r->_phase > 0.001f) {
            const float dotMaxR = outerR * r->dotScale;
            const float dotR = dotMaxR * r->_phase;
            UIColor dc = dotC;
            dc.a *= r->_phase;
            DrawCircle(window->sdlRenderer, cx, cy, dotR, dc);
        }
}

static void RenderWidget_Switch(UIWindow* window, UIWidget* el,
                                UIWidgetBase* base, float op) {

        UISwitch* sw = (UISwitch*)base;
        if (!el->width || !el->height) return;

        const float w = *el->width;
        const float h = *el->height;

        // Animate _phase toward the on/off target.
        const float target = sw->on ? 1.0f : 0.0f;
        if (sw->animMs > 0) {
            // Frame-rate independent ease: move ~70% of the remaining
            // distance every animMs (close enough for the eye).
            const float k = 1.0f - powf(0.3f, 1.0f / (float)sw->animMs);
            sw->_phase += (target - sw->_phase) * k * 16.0f; // assume ~16ms/frame
            if ((target == 1.0f && sw->_phase > 0.999f) ||
                (target == 0.0f && sw->_phase < 0.001f)) {
                sw->_phase = target;
            }
        } else {
            sw->_phase = target;
        }

        // Track (pill background) blends from off to on colour.
        UIColor tc;
        tc.r = sw->offColor.r + (sw->onColor.r - sw->offColor.r) * sw->_phase;
        tc.g = sw->offColor.g + (sw->onColor.g - sw->offColor.g) * sw->_phase;
        tc.b = sw->offColor.b + (sw->onColor.b - sw->offColor.b) * sw->_phase;
        tc.a = (sw->offColor.a + (sw->onColor.a - sw->offColor.a) * sw->_phase) * op;

        SDL_FRect track = { el->x, el->y, w, h };
        const float trackRadius = h * 0.5f;
        UIColor bc = sw->borderColor; bc.a *= op;
        DrawRoundedRectWithBorder(window->sdlRenderer, track,
                                  tc, trackRadius,
                                  (int)sw->borderWidth, bc);

        // Knob slides between the off/on positions.
        const float m = sw->knobMargin;
        const float knobR = SDL_max(0.0f, h * 0.5f - m);
        const float minX = el->x + m + knobR;
        const float maxX = el->x + w - m - knobR;
        const float knobX = minX + (maxX - minX) * sw->_phase;
        const float knobY = el->y + h * 0.5f;
        UIColor kc = sw->knobColor; kc.a *= op;
        DrawCircle(window->sdlRenderer, knobX, knobY, knobR, kc);
}

static void RenderWidget_Tooltip(UIWindow* window, UIWidget* el,
                                UIWidgetBase* base, float op) {
    (void)el;


        UITooltip* tt = (UITooltip*)base;
        if (!tt->_visible || !tt->text || !*tt->text) return;

        TTF_Font* font = (tt->fontFamily && tt->fontSize > 0.0f)
            ? GetFont(tt->fontFamily, tt->fontSize) : NULL;
        if (!font) return;
        TTF_SetFontStyle(font, TTF_STYLE_NORMAL);

        const int textLen = (int)strlen(tt->text);
        if (!tt->__SDL_textTexture || tt->__cachedTextLen != textLen) {
            if (tt->__SDL_textTexture) {
                SDL_DestroyTexture(tt->__SDL_textTexture);
                tt->__SDL_textTexture = NULL;
            }
            SDL_Color sc = {(Uint8)tt->textColor.r,(Uint8)tt->textColor.g,
                            (Uint8)tt->textColor.b,255};
            SDL_Surface* surf = TTF_RenderText_Blended(font, tt->text, (size_t)textLen, sc);
            if (surf) {
                tt->__SDL_textTexture = SDL_CreateTextureFromSurface(
                    window->sdlRenderer, surf);
                SDL_DestroySurface(surf);
                if (tt->__SDL_textTexture) {
                    SDL_SetTextureScaleMode(tt->__SDL_textTexture, SDL_SCALEMODE_LINEAR);
                }
            }
            tt->__cachedTextLen = textLen;
        }
        if (!tt->__SDL_textTexture) return;

        float tw = 0.0f, th = 0.0f;
        SDL_GetTextureSize(tt->__SDL_textTexture, &tw, &th);
        const float bw = tw + tt->paddingX * 2.0f;
        const float bh = th + tt->paddingY * 2.0f;
        // Position next to the cursor with a small offset; clamp to window.
        int winW = 0, winH = 0;
        SDL_GetWindowSize(window->sdlWindow, &winW, &winH);
        float bx = tt->_cursorX + 14.0f;
        float by = tt->_cursorY + 14.0f;
        if (bx + bw > winW) bx = winW - bw - 4.0f;
        if (by + bh > winH) by = winH - bh - 4.0f;

        SDL_FRect bg = { bx, by, bw, bh };
        UIColor bgC = tt->bgColor; bgC.a *= op;
        DrawDropShadow(window->sdlRenderer, bg, tt->radius, (UIShadow){
            .offsetX=0, .offsetY=2, .blur=10, .spread=0,
            .color={0,0,0,0.35f * op}
        });
        DrawRoundedRectFill(window->sdlRenderer, bg, bgC, tt->radius);

        SDL_FRect dst = { bx + tt->paddingX, by + tt->paddingY, tw, th };
        SDL_SetTextureAlphaMod(tt->__SDL_textTexture, (Uint8)(op * 255.0f));
        SDL_RenderTexture(window->sdlRenderer, tt->__SDL_textTexture, NULL, &dst);
        SDL_SetTextureAlphaMod(tt->__SDL_textTexture, 255);
}

static void RenderWidget_Menu(UIWindow* window, UIWidget* el,
                                UIWidgetBase* base, float op) {
    (void)el;


        UIMenu* m = (UIMenu*)base;
        if (!m->visible || m->itemCount <= 0) return;

        const float w = (m->itemWidth > 0.0f) ? m->itemWidth : 180.0f;
        const float h = m->paddingY * 2.0f + m->itemCount * m->itemHeight;
        SDL_FRect rect = { m->anchorX, m->anchorY, w, h };

        DrawDropShadow(window->sdlRenderer, rect, m->radius, (UIShadow){
            .offsetX=0, .offsetY=8, .blur=20, .spread=-2,
            .color={0,0,0,0.25f * op}
        });
        UIColor bg = m->bgColor;     bg.a *= op;
        UIColor br = m->borderColor; br.a *= op;
        DrawRoundedRectWithBorder(window->sdlRenderer, rect, bg, m->radius,
                                  (int)m->borderWidth, br);

        TTF_Font* font = (m->fontFamily && m->fontSize > 0.0f)
            ? GetFont(m->fontFamily, m->fontSize) : NULL;
        if (font) TTF_SetFontStyle(font, TTF_STYLE_NORMAL);

        for (int i = 0; i < m->itemCount; i++) {
            SDL_FRect itemR = {
                rect.x + 4.0f,
                rect.y + m->paddingY + i * m->itemHeight,
                rect.w - 8.0f,
                m->itemHeight
            };
            if (i == m->hoverIndex) {
                UIColor hc = m->itemHoverColor; hc.a *= op;
                DrawRoundedRectFill(window->sdlRenderer, itemR, hc, 4.0f);
            }
            if (font && m->labels[i]) {
                UIColor tc = m->textColor; tc.a *= op;
                SDL_Color sc = {(Uint8)tc.r,(Uint8)tc.g,(Uint8)tc.b,255};
                const size_t len = strlen(m->labels[i]);
                SDL_Surface* surf = TTF_RenderText_Blended(font, m->labels[i], len, sc);
                if (surf) {
                    SDL_Texture* tex = SDL_CreateTextureFromSurface(window->sdlRenderer, surf);
                    if (tex) {
                        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
                        float tw = 0, th = 0;
                        SDL_GetTextureSize(tex, &tw, &th);
                        SDL_FRect dst = { itemR.x + m->paddingX,
                                          itemR.y + (m->itemHeight - th) * 0.5f,
                                          tw, th };
                        SDL_RenderTexture(window->sdlRenderer, tex, NULL, &dst);
                        SDL_DestroyTexture(tex);
                    }
                    SDL_DestroySurface(surf);
                }
            }
        }
}

static void RenderWidget_Dropdown(UIWindow* window, UIWidget* el,
                                UIWidgetBase* base, float op) {

        UIDropdown* d = (UIDropdown*)base;
        if (!el->width || !el->height) return;
        SDL_FRect btn = { el->x, el->y, *el->width, *el->height };

        UIColor bg = d->bgColor;     bg.a *= op;
        UIColor br = d->borderColor; br.a *= op;
        UIColor tc = d->textColor;   tc.a *= op;
        DrawRoundedRectWithBorder(window->sdlRenderer, btn, bg, d->radius,
                                  (int)d->borderWidth, br);

        TTF_Font* font = (d->fontFamily && d->fontSize > 0.0f)
            ? GetFont(d->fontFamily, d->fontSize) : NULL;
        if (font) TTF_SetFontStyle(font, TTF_STYLE_NORMAL);

        const char* label = (d->selectedIndex >= 0 && d->selectedIndex < d->itemCount)
            ? d->labels[d->selectedIndex] : "(select)";
        if (font && label) {
            SDL_Color sc = {(Uint8)tc.r,(Uint8)tc.g,(Uint8)tc.b,255};
            const size_t len = strlen(label);
            SDL_Surface* surf = TTF_RenderText_Blended(font, label, len, sc);
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(window->sdlRenderer, surf);
                if (tex) {
                    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
                    float tw = 0, th = 0;
                    SDL_GetTextureSize(tex, &tw, &th);
                    SDL_FRect dst = { btn.x + d->paddingX,
                                      btn.y + (btn.h - th) * 0.5f,
                                      tw, th };
                    SDL_RenderTexture(window->sdlRenderer, tex, NULL, &dst);
                    SDL_DestroyTexture(tex);
                }
                SDL_DestroySurface(surf);
            }
        }

        // Chevron marker.
        const float chevSize = 6.0f;
        const float cxv = btn.x + btn.w - d->paddingX - chevSize;
        const float cyv = btn.y + btn.h * 0.5f - chevSize * 0.3f;
        SDL_FRect chev = { cxv, cyv, chevSize, chevSize };
        DrawRoundedRectFill(window->sdlRenderer, chev, tc, 1.0f);

        // Popup list when open.
        if (d->open && d->itemCount > 0) {
            const float popY = btn.y + btn.h + 4.0f;
            const float popH = d->itemCount * d->popupItemHeight;
            SDL_FRect popR = { btn.x, popY, btn.w, popH };
            DrawDropShadow(window->sdlRenderer, popR, d->radius, (UIShadow){
                .offsetX=0, .offsetY=8, .blur=18, .spread=-2,
                .color={0,0,0,0.25f * op}
            });
            DrawRoundedRectWithBorder(window->sdlRenderer, popR, bg, d->radius,
                                      (int)d->borderWidth, br);

            for (int i = 0; i < d->itemCount; i++) {
                SDL_FRect rowR = { popR.x + 2.0f,
                                   popR.y + i * d->popupItemHeight,
                                   popR.w - 4.0f, d->popupItemHeight };
                if (i == d->hoverIndex) {
                    UIColor hc = d->itemHoverColor; hc.a *= op;
                    DrawRoundedRectFill(window->sdlRenderer, rowR, hc, 4.0f);
                }
                if (font && d->labels[i]) {
                    SDL_Color sc = {(Uint8)tc.r,(Uint8)tc.g,(Uint8)tc.b,255};
                    const size_t len = strlen(d->labels[i]);
                    SDL_Surface* surf = TTF_RenderText_Blended(font, d->labels[i], len, sc);
                    if (surf) {
                        SDL_Texture* tex = SDL_CreateTextureFromSurface(window->sdlRenderer, surf);
                        if (tex) {
                            SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
                            float tw = 0, th = 0;
                            SDL_GetTextureSize(tex, &tw, &th);
                            SDL_FRect dst = { rowR.x + d->paddingX,
                                              rowR.y + (d->popupItemHeight - th) * 0.5f,
                                              tw, th };
                            SDL_RenderTexture(window->sdlRenderer, tex, NULL, &dst);
                            SDL_DestroyTexture(tex);
                        }
                        SDL_DestroySurface(surf);
                    }
                }
            }
        }
}


#include <uikit/reactive.h>

#include <stdlib.h>
#include <string.h>

#include <uikit/mocida_alloc.h>

#ifdef _MSC_VER
#  define UI_STRDUP _strdup
#else
#  define UI_STRDUP strdup
#endif

/**
 * One subscriber attached to a UISignal. Subscriptions are linked
 * together into a per-signal list and tombstoned (dead == 1) rather
 * than removed mid-notification to keep iteration safe during reentry.
 */
struct UISubscription {
    UISignalCallback cb;       /**< User callback invoked on every signal change. */
    void* userdata;            /**< Opaque pointer forwarded to `cb`. */
    UISignal* owner;           /**< Signal this subscription is attached to. */
    struct UISubscription* next; /**< Next subscription in the signal's list. */
    int dead;                  /**< Tombstoned during notify; reaped afterwards. */
};

/**
 * Reactive value with a tagged-union payload. Subscribers registered
 * via UISignal_Subscribe fire whenever the value changes through
 * UISignal_Set*. Reentrant sets during a notification are blocked.
 */
struct UISignal {
    UISignalType type;         /**< Tag selecting which member of `val` is live. */
    union {
        int i;                 /**< Integer payload (type == UI_SIG_INT). */
        float f;                /**< Float payload (type == UI_SIG_FLOAT). */
        char* s;               /**< Owned string (type == UI_SIG_STRING). */
        void* p;               /**< Borrowed pointer (type == UI_SIG_PTR). */
    } val;                     /**< Current signal value. */
    UISubscription* subs;      /**< Head of the subscription linked list. */
    int notifying;             /**< Reentrancy guard - blocks recursive Set during dispatch. */
    int pending_reap;          /**< >0 if any subscription was tombstoned during notify. */
};

/* ------------------------------------------------------------------ */
/* internals                                                          */
/* ------------------------------------------------------------------ */

static UISignal* uisig_new(UISignalType t) {
    UISignal* s = (UISignal*)calloc(1, sizeof(UISignal));
    if (!s) return NULL;
    s->type = t;
    return s;
}

static void uisig_reap(UISignal* s) {
    UISubscription** link = &s->subs;
    while (*link) {
        UISubscription* sub = *link;
        if (sub->dead) {
            *link = sub->next;
            free(sub);
        } else {
            link = &sub->next;
        }
    }
    s->pending_reap = 0;
}

static void uisig_notify(UISignal* s) {
    if (s->notifying) return; /* drop recursive notifications */
    s->notifying = 1;
    for (UISubscription* it = s->subs; it; it = it->next) {
        if (!it->dead) it->cb(s, it->userdata);
    }
    s->notifying = 0;
    if (s->pending_reap) uisig_reap(s);
}

/* ------------------------------------------------------------------ */
/* constructors                                                       */
/* ------------------------------------------------------------------ */

UISignal* UISignal_CreateInt(int v) {
    UISignal* s = uisig_new(UI_SIGNAL_INT);
    if (s) s->val.i = v;
    return s;
}

UISignal* UISignal_CreateFloat(float v) {
    UISignal* s = uisig_new(UI_SIGNAL_FLOAT);
    if (s) s->val.f = v;
    return s;
}

UISignal* UISignal_CreateString(const char* v) {
    UISignal* s = uisig_new(UI_SIGNAL_STRING);
    if (!s) return NULL;
    s->val.s = UI_STRDUP(v ? v : "");
    if (!s->val.s) { free(s); return NULL; }
    return s;
}

UISignal* UISignal_CreatePointer(void* v) {
    UISignal* s = uisig_new(UI_SIGNAL_POINTER);
    if (s) s->val.p = v;
    return s;
}

/* ------------------------------------------------------------------ */
/* accessors                                                          */
/* ------------------------------------------------------------------ */

UISignalType UISignal_GetType(const UISignal* s) {
    return s ? s->type : UI_SIGNAL_INT;
}

int UISignal_GetInt(const UISignal* s) {
    return (s && s->type == UI_SIGNAL_INT) ? s->val.i : 0;
}

float UISignal_GetFloat(const UISignal* s) {
    return (s && s->type == UI_SIGNAL_FLOAT) ? s->val.f : 0.0f;
}

const char* UISignal_GetString(const UISignal* s) {
    return (s && s->type == UI_SIGNAL_STRING) ? s->val.s : NULL;
}

void* UISignal_GetPointer(const UISignal* s) {
    return (s && s->type == UI_SIGNAL_POINTER) ? s->val.p : NULL;
}

/* ------------------------------------------------------------------ */
/* setters                                                            */
/* ------------------------------------------------------------------ */

void UISignal_SetInt(UISignal* s, int v) {
    if (!s || s->type != UI_SIGNAL_INT) return;
    if (s->val.i == v) return;
    s->val.i = v;
    uisig_notify(s);
}

void UISignal_SetFloat(UISignal* s, float v) {
    if (!s || s->type != UI_SIGNAL_FLOAT) return;
    /* Bitwise compare so NaN != NaN doesn't loop on identical writes. */
    if (memcmp(&s->val.f, &v, sizeof(float)) == 0) return;
    s->val.f = v;
    uisig_notify(s);
}

void UISignal_SetString(UISignal* s, const char* v) {
    if (!s || s->type != UI_SIGNAL_STRING) return;
    const char* incoming = v ? v : "";
    if (s->val.s && strcmp(s->val.s, incoming) == 0) return;
    char* dup = UI_STRDUP(incoming);
    if (!dup) return; /* OOM: keep old value, do not notify */
    free(s->val.s);
    s->val.s = dup;
    uisig_notify(s);
}

void UISignal_SetPointer(UISignal* s, void* v) {
    if (!s || s->type != UI_SIGNAL_POINTER) return;
    if (s->val.p == v) return;
    s->val.p = v;
    uisig_notify(s);
}

void UISignal_Notify(UISignal* s) {
    if (s) uisig_notify(s);
}

/* ------------------------------------------------------------------ */
/* subscriptions                                                      */
/* ------------------------------------------------------------------ */

UISubscription* UISignal_Subscribe(UISignal* s, UISignalCallback cb, void* userdata) {
    if (!s || !cb) return NULL;
    UISubscription* sub = (UISubscription*)malloc(sizeof(UISubscription));
    if (!sub) return NULL;
    sub->cb = cb;
    sub->userdata = userdata;
    sub->owner = s;
    sub->dead = 0;
    sub->next = s->subs;
    s->subs = sub;
    return sub;
}

void UISignal_Unsubscribe(UISubscription* sub) {
    if (!sub) return;
    UISignal* s = sub->owner;
    if (s && s->notifying) {
        /* Tombstone now, reap after the current notify completes. */
        sub->dead = 1;
        s->pending_reap = 1;
        return;
    }
    if (s) {
        UISubscription** link = &s->subs;
        while (*link && *link != sub) link = &(*link)->next;
        if (*link == sub) *link = sub->next;
    }
    free(sub);
}

void UISignal_Destroy(UISignal* s) {
    if (!s) return;
    UISubscription* it = s->subs;
    while (it) {
        UISubscription* nx = it->next;
        free(it);
        it = nx;
    }
    if (s->type == UI_SIGNAL_STRING) free(s->val.s);
    free(s);
}

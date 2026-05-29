// Video widget. The public API is the same on every platform; the
// implementation is selected at compile time:
//
//   - Windows: Media Foundation (hardware-decoded via Windows codecs).
//   - Linux  : GStreamer playbin (when libgstreamer-dev was found
//              at configure time — MOCIDA_HAS_GSTREAMER is set in
//              CMakeLists). Routes to NVDEC / VAAPI / V4L2 when the
//              corresponding plugins are installed.
//   - Other  : stub backend; UIVideo_Create returns NULL.

#include <uikit/video.h>
#include <uikit/debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32

// Media Foundation pulls in COM (ole32 / mfplat / mfreadwrite / mfuuid).
// CMake links these for us; here we just include the headers.
#define COBJMACROS               // C-style helper macros for IMF* methods
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <propvarutil.h>

struct UIVideo {
    const char* __widget_type;

    // ----- Resource handles -----
    IMFSourceReader* reader;
    SDL_Texture*     texture;
    SDL_AudioStream* audio;
    char*            source;

    // ----- Stream info -----
    int    videoWidth;
    int    videoHeight;
    int    hasVideo;
    int    hasAudio;
    int    audioChannels;
    int    audioSampleRate;
    double durationSec;       // cached on Create from MF_PD_DURATION

    // ----- Playback state -----
    int      playing;
    int      loop;
    int      muted;
    int      eof;
    float    volume;
    UIFillMode fillMode;

    // Clock (seconds since start of playback / last seek).
    double   clockSec;
    Uint64   lastTickMs;

    // Timestamp (seconds) of the frame currently uploaded into texture.
    double      currentFrameTime;
    // Next decoded sample we already pulled ahead of the clock - kept
    // as the raw IMFSample so we can either upload it now or release
    // it cleanly on Seek/Destroy.
    IMFSample*  pendingSample;
    double      pendingFrameTime;
    int         pendingFrameValid;

    // Callbacks
    UIVideoCallback onEnded;
    void*           userdata;
};

static int g_mfInited = 0;

static void EnsureMFInit(void) {
    if (g_mfInited) return;
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        // RPC_E_CHANGED_MODE just means COM was already initialised in
        // a different apartment, not actually a failure.
        UI_ERROR(UI_CAT_VIDEO, "CoInitializeEx failed 0x%08lx", hr);
    }
    hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(hr)) {
        UI_ERROR(UI_CAT_VIDEO, "MFStartup failed 0x%08lx", hr);
        return;
    }
    g_mfInited = 1;
}

// Converts a UTF-8 path to UTF-16 for MF's URL APIs.
static wchar_t* Utf8ToUtf16(const char* utf8) {
    if (!utf8) return NULL;
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (len <= 0) return NULL;
    wchar_t* out = (wchar_t*)malloc(sizeof(wchar_t) * (size_t)len);
    if (!out) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, len);
    return out;
}

// Asks MF to give us NV12 frames. NV12 is THE canonical decoded
// video format on Windows - every codec MF supports (H.264, HEVC,
// VP9, AV1, ...) decodes natively or near-natively to NV12 via the
// in-decoder color converter. By requesting NV12 we sidestep the
// Video Processor MFT entirely, which avoids the "VIDEO_PROCESSING
// pretends to work but quietly emits the codec's native YUV" failure
// mode we hit on some HEVC / unusual H.264 sources.
//
// SDL3 has a built-in NV12 pixel format (SDL_PIXELFORMAT_NV12); the
// renderer converts YUV -> RGB on the GPU using its shader. So the
// per-frame CPU cost ends up being a memcpy of the Y and UV planes.
static int ConfigureVideoOutput(IMFSourceReader* reader) {
    IMFMediaType* out = NULL;
    HRESULT hr = MFCreateMediaType(&out);
    if (FAILED(hr)) { UI_ERROR(UI_CAT_VIDEO, "MFCreateMediaType failed 0x%08lx", hr); return 0; }

    hr = IMFMediaType_SetGUID(out, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    if (FAILED(hr)) goto fail;
    hr = IMFMediaType_SetGUID(out, &MF_MT_SUBTYPE, &MFVideoFormat_NV12);
    if (FAILED(hr)) goto fail;

    hr = IMFSourceReader_SetCurrentMediaType(
        reader,
        (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        NULL, out);
    IMFMediaType_Release(out);
    if (FAILED(hr)) {
        UI_ERROR(UI_CAT_VIDEO,
                 "SetCurrentMediaType(NV12) failed 0x%08lx "
                 "(codec missing? install HEVC extension from MS Store for h.265)",
                 hr);
        return 0;
    }
    return 1;

fail:
    IMFMediaType_Release(out);
    UI_ERROR(UI_CAT_VIDEO, "media type setup failed 0x%08lx", hr);
    return 0;
}

static int ConfigureAudioOutput(IMFSourceReader* reader,
                                int* outChannels, int* outRate) {
    IMFMediaType* out = NULL;
    HRESULT hr = MFCreateMediaType(&out);
    if (FAILED(hr)) return 0;

    hr = IMFMediaType_SetGUID(out, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    if (FAILED(hr)) goto fail;
    hr = IMFMediaType_SetGUID(out, &MF_MT_SUBTYPE, &MFAudioFormat_PCM);
    if (FAILED(hr)) goto fail;
    hr = IMFMediaType_SetUINT32(out, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    if (FAILED(hr)) goto fail;
    hr = IMFMediaType_SetUINT32(out, &MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
    if (FAILED(hr)) goto fail;
    hr = IMFMediaType_SetUINT32(out, &MF_MT_AUDIO_NUM_CHANNELS, 2);
    if (FAILED(hr)) goto fail;
    hr = IMFMediaType_SetUINT32(out, &MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
    if (FAILED(hr)) goto fail;
    hr = IMFMediaType_SetUINT32(out, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 48000 * 4);
    if (FAILED(hr)) goto fail;

    hr = IMFSourceReader_SetCurrentMediaType(
        reader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, out);

    IMFMediaType_Release(out);
    if (FAILED(hr)) return 0;

    *outChannels = 2;
    *outRate     = 48000;
    return 1;

fail:
    IMFMediaType_Release(out);
    return 0;
}

static int QueryVideoSize(IMFSourceReader* reader, int* outW, int* outH) {
    IMFMediaType* mt = NULL;
    HRESULT hr = IMFSourceReader_GetCurrentMediaType(
        reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &mt);
    if (FAILED(hr)) return 0;

    // MF_MT_FRAME_SIZE is a UINT64 packed as (width << 32) | height.
    // The C++ helper MFGetAttributeSize unpacks it but is C++-only,
    // so we do the unpack inline here.
    UINT64 packed = 0;
    hr = IMFAttributes_GetUINT64((IMFAttributes*)mt, &MF_MT_FRAME_SIZE, &packed);
    IMFMediaType_Release(mt);
    if (FAILED(hr)) return 0;
    *outW = (int)(packed >> 32);
    *outH = (int)(packed & 0xFFFFFFFFu);
    return 1;
}

// ---------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------

UIVideo* UIVideo_Create(const char* path) {
    EnsureMFInit();
    if (!path) return NULL;

    UIVideo* v = (UIVideo*)calloc(1, sizeof(UIVideo));
    if (!v) return NULL;
    v->__widget_type = UI_WIDGET_VIDEO;
    v->source   = _strdup(path);
    v->loop     = 0;
    v->playing  = 0;
    v->volume   = 1.0f;
    v->fillMode = FILL_FIT;

    // Reader attributes. We no longer enable the Video Processor MFT -
    // we read NV12 straight from the codec and let SDL convert to RGB
    // on the GPU. That removes a whole class of "VIDEO_PROCESSING
    // silently produces broken output" issues on HEVC / unusual H.264.
    // We do keep HARDWARE_TRANSFORMS on so hardware decoders (Intel
    // QSV, NVDEC, AMD UVD) are eligible.
    IMFAttributes* readerAttrs = NULL;
    if (SUCCEEDED(MFCreateAttributes(&readerAttrs, 1))) {
        IMFAttributes_SetUINT32(readerAttrs,
            &MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    }

    // Try the path as-is, then relative to the exe.
    wchar_t* wpath = Utf8ToUtf16(path);
    IMFSourceReader* reader = NULL;
    HRESULT hr = E_FAIL;
    if (wpath) {
        hr = MFCreateSourceReaderFromURL(wpath, readerAttrs, &reader);
        free(wpath);
    }
    if (FAILED(hr) || !reader) {
        const char* base = SDL_GetBasePath();
        if (base) {
            char buf[1024];
            int n = snprintf(buf, sizeof(buf), "%s%s", base, path);
            if (n > 0 && n < (int)sizeof(buf)) {
                wpath = Utf8ToUtf16(buf);
                if (wpath) {
                    hr = MFCreateSourceReaderFromURL(wpath, readerAttrs, &reader);
                    free(wpath);
                }
            }
        }
    }
    if (readerAttrs) IMFAttributes_Release(readerAttrs);

    if (FAILED(hr) || !reader) {
        UI_ERROR(UI_CAT_VIDEO, "failed to open '%s' (0x%08lx)", path, hr);
        free(v->source);
        free(v);
        return NULL;
    }
    v->reader = reader;

    // Enable only the streams we care about. SetStreamSelection with
    // ALL_STREAMS,FALSE first disables every track; the next two calls
    // turn on video + audio individually.
    IMFSourceReader_SetStreamSelection(reader, (DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);

    HRESULT hrVideoSel = IMFSourceReader_SetStreamSelection(reader,
        (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
    if (FAILED(hrVideoSel)) {
        UI_ERROR(UI_CAT_VIDEO, "SetStreamSelection(video) failed 0x%08lx", hrVideoSel);
    } else if (!ConfigureVideoOutput(reader)) {
        // ConfigureVideoOutput already prints its own HRESULT.
    } else if (!QueryVideoSize(reader, &v->videoWidth, &v->videoHeight)) {
        UI_ERROR(UI_CAT_VIDEO, "QueryVideoSize failed for '%s'", path);
    } else {
        v->hasVideo = 1;
    }

    HRESULT hrAudioSel = IMFSourceReader_SetStreamSelection(reader,
        (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
    if (SUCCEEDED(hrAudioSel) &&
        ConfigureAudioOutput(reader, &v->audioChannels, &v->audioSampleRate)) {
        v->hasAudio = 1;
    }

    if (!v->hasVideo) {
        UI_ERROR(UI_CAT_VIDEO, "no decodable video stream in '%s' "
                               "(unsupported codec or DRM-protected)", path);
        UIVideo_Destroy(v);
        return NULL;
    }

    // Cache total duration. MF_PD_DURATION is a 100-ns counter on the
    // presentation descriptor; not all sources report it (live streams
    // for example), so fall back to 0 in that case.
    {
        PROPVARIANT durPv;
        PropVariantInit(&durPv);
        if (SUCCEEDED(IMFSourceReader_GetPresentationAttribute(
                reader,
                (DWORD)MF_SOURCE_READER_MEDIASOURCE,
                &MF_PD_DURATION, &durPv))) {
            v->durationSec = (double)durPv.uhVal.QuadPart / 10000000.0;
        }
        PropVariantClear(&durPv);
    }

    // Allocate the audio stream lazily on first sample - we need the
    // SDL audio subsystem up, but UIVideo_Create can be called before
    // the window/app is fully initialised.
    return v;
}

UIVideo* UIVideo_Play(UIVideo* v) {
    if (!v) return v;
    if (!v->playing) {
        v->playing = 1;
        v->lastTickMs = SDL_GetTicks();
    }
    if (v->eof && v->loop) {
        // Seek to start.
        PROPVARIANT pv = {0};
        pv.vt = VT_I8;
        pv.hVal.QuadPart = 0;
        IMFSourceReader_SetCurrentPosition(v->reader, &GUID_NULL, &pv);
        v->eof = 0;
        v->clockSec = 0.0;
        v->currentFrameTime = -1.0;
        if (v->pendingSample) {
            IMFSample_Release(v->pendingSample);
            v->pendingSample = NULL;
        }
        v->pendingFrameValid = 0;
    }
    return v;
}

UIVideo* UIVideo_Pause(UIVideo* v) {
    if (!v) return v;
    v->playing = 0;
    return v;
}

UIVideo* UIVideo_Stop(UIVideo* v) {
    if (!v) return v;
    v->playing = 0;
    v->clockSec = 0.0;
    v->currentFrameTime = -1.0;
    if (v->pendingSample) {
        IMFSample_Release(v->pendingSample);
        v->pendingSample = NULL;
    }
    v->pendingFrameValid = 0;
    if (v->audio) SDL_ClearAudioStream(v->audio);
    PROPVARIANT pv = {0};
    pv.vt = VT_I8;
    pv.hVal.QuadPart = 0;
    IMFSourceReader_SetCurrentPosition(v->reader, &GUID_NULL, &pv);
    v->eof = 0;
    return v;
}

UIVideo* UIVideo_SetLoop(UIVideo* v, int loop) {
    if (v) v->loop = loop ? 1 : 0;
    return v;
}

UIVideo* UIVideo_SetVolume(UIVideo* v, float volume) {
    if (!v) return v;
    if (volume < 0.0f) volume = 0.0f;
    v->volume = volume;
    if (v->audio) SDL_SetAudioStreamGain(v->audio, v->muted ? 0.0f : volume);
    return v;
}

UIVideo* UIVideo_SetMuted(UIVideo* v, int muted) {
    if (!v) return v;
    v->muted = muted ? 1 : 0;
    if (v->audio) SDL_SetAudioStreamGain(v->audio, v->muted ? 0.0f : v->volume);
    return v;
}

UIVideo* UIVideo_SetFillMode(UIVideo* v, UIFillMode mode) {
    if (v) v->fillMode = mode;
    return v;
}

UIVideo* UIVideo_OnEnded(UIVideo* v, UIVideoCallback cb, void* userdata) {
    if (!v) return v;
    v->onEnded  = cb;
    v->userdata = userdata;
    return v;
}

int UIVideo_IsPlaying(UIVideo* v) { return v && v->playing && !v->eof; }
int UIVideo_GetWidth (UIVideo* v) { return v ? v->videoWidth  : 0; }
int UIVideo_GetHeight(UIVideo* v) { return v ? v->videoHeight : 0; }
double UIVideo_GetTime    (UIVideo* v){ return v ? v->clockSec    : 0.0; }
double UIVideo_GetDuration(UIVideo* v){ return v ? v->durationSec : 0.0; }

void UIVideo_Seek(UIVideo* v, double seconds) {
    if (!v || !v->reader) return;
    if (seconds < 0.0) seconds = 0.0;
    if (v->durationSec > 0.0 && seconds > v->durationSec) seconds = v->durationSec;

    PROPVARIANT pv = {0};
    pv.vt = VT_I8;
    pv.hVal.QuadPart = (LONGLONG)(seconds * 10000000.0);
    HRESULT hr = IMFSourceReader_SetCurrentPosition(v->reader, &GUID_NULL, &pv);
    if (FAILED(hr)) {
        UI_ERROR(UI_CAT_VIDEO, "seek to %.2fs failed 0x%08lx", seconds, hr);
        return;
    }
    // Drop any data buffered for the OLD position so playback resumes
    // cleanly at the new one.
    v->clockSec          = seconds;
    v->currentFrameTime  = -1.0;
    if (v->pendingSample) {
        IMFSample_Release(v->pendingSample);
        v->pendingSample = NULL;
    }
    v->pendingFrameValid = 0;
    v->eof               = 0;
    if (v->audio) SDL_ClearAudioStream(v->audio);
}

// No-op on Windows: Media Foundation initialises per-instance inside
// UIVideo_Create and doesn't suffer the GLib/SDL conflict that makes
// UIVideo_PreInit necessary on Linux. Defined here so callers can
// portably invoke it at app startup.
void UIVideo_PreInit(void) { /* nothing to do */ }

void UIVideo_Destroy(UIVideo* v) {
    if (!v) return;
    if (v->pendingSample) IMFSample_Release(v->pendingSample);
    if (v->reader)  IMFSourceReader_Release(v->reader);
    if (v->texture) SDL_DestroyTexture(v->texture);
    if (v->audio)   SDL_DestroyAudioStream(v->audio);
    free(v->source);
    free(v);
}

// ---------------------------------------------------------------------
// Tick / decode (called from window.c each frame)
// ---------------------------------------------------------------------

// Reads one sample from the given stream. Returns SDL_TRUE if a sample
// was decoded and copied into *outSample (caller must Release). On EOF
// sets *eof and returns SDL_FALSE.
static int ReadOneSample(IMFSourceReader* reader, DWORD stream,
                         IMFSample** outSample, LONGLONG* outTs100ns,
                         int* eof) {
    DWORD flags = 0;
    LONGLONG ts = 0;
    *outSample = NULL;
    HRESULT hr = IMFSourceReader_ReadSample(
        reader, stream, 0, NULL, &flags, &ts, outSample);
    if (FAILED(hr)) return 0;
    if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
        if (eof) *eof = 1;
        if (*outSample) { IMFSample_Release(*outSample); *outSample = NULL; }
        return 0;
    }
    if (!*outSample) return 0;
    *outTs100ns = ts;
    return 1;
}

// Pulls audio samples until we're "ahead" of playback by ~200ms of
// queued audio. Caller-friendly: bounded work per call.
static void PumpAudio(UIVideo* v) {
    if (!v->hasAudio) return;

    if (!v->audio) {
        // SDL_INIT_AUDIO is NOT initialised by UIWindow_Create (which
        // only sets up SDL_INIT_VIDEO). If we're the first system to
        // want audio, bring the subsystem up here - otherwise
        // SDL_OpenAudioDeviceStream returns NULL with no warning.
        if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO)) {
            if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
                UI_ERROR(UI_CAT_VIDEO, "SDL_INIT_AUDIO failed: %s", SDL_GetError());
                v->hasAudio = 0;
                return;
            }
        }

        SDL_AudioSpec spec = { SDL_AUDIO_S16LE, v->audioChannels, v->audioSampleRate };
        v->audio = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
        if (!v->audio) {
            UI_ERROR(UI_CAT_VIDEO, "SDL_OpenAudioDeviceStream failed: %s",
                    SDL_GetError());
            // Don't retry every frame - audio is unrecoverable for this video.
            v->hasAudio = 0;
            return;
        }
        SDL_SetAudioStreamGain(v->audio, v->muted ? 0.0f : v->volume);
        SDL_ResumeAudioStreamDevice(v->audio);
    }
    if (!v->audio) return;

    // Keep about 200ms queued ahead. Each S16 stereo sample = 4 bytes.
    const int bytesPerSec = v->audioSampleRate * 4;
    const int wantBytes = bytesPerSec / 5;

    while (SDL_GetAudioStreamAvailable(v->audio) < wantBytes) {
        IMFSample* s = NULL;
        LONGLONG ts = 0;
        int eof = 0;
        if (!ReadOneSample(v->reader,
                           (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                           &s, &ts, &eof)) {
            if (eof) v->eof = 1;
            break;
        }
        IMFMediaBuffer* buf = NULL;
        if (FAILED(IMFSample_ConvertToContiguousBuffer(s, &buf))) {
            IMFSample_Release(s);
            break;
        }
        BYTE* data = NULL; DWORD curLen = 0;
        if (SUCCEEDED(IMFMediaBuffer_Lock(buf, &data, NULL, &curLen)) && data && curLen > 0) {
            SDL_PutAudioStreamData(v->audio, data, (int)curLen);
            IMFMediaBuffer_Unlock(buf);
        }
        IMFMediaBuffer_Release(buf);
        IMFSample_Release(s);
    }
}

// Uploads an NV12 frame from MF's media buffer directly into an
// SDL_PIXELFORMAT_NV12 streaming texture. SDL converts YUV -> RGB on
// the GPU during render, so we only spend a memcpy here.
//
// NV12 layout in MF's buffer:
//   [ Y plane: pitch * height bytes ]
//   [ UV plane interleaved: pitch * (height/2) bytes  - same pitch ]
// pitch >= width (often padded to 16 / 32 / 64-byte alignment).
//
// IMF2DBuffer's Lock2D returns the Y-plane base + its pitch; the UV
// plane immediately follows in memory. We pass both to
// SDL_UpdateNVTexture which respects each plane's pitch.
static int UploadNV12Frame(IMFMediaBuffer* buf, SDL_Texture* texture,
                           int w, int h) {
    if (!buf || !texture || w <= 0 || h <= 0) return 0;

    IMF2DBuffer* buf2d = NULL;
    if (SUCCEEDED(IMFMediaBuffer_QueryInterface(buf, &IID_IMF2DBuffer, (void**)&buf2d))
        && buf2d) {
        BYTE* scan0 = NULL;
        LONG  pitch = 0;
        int ok = 0;
        if (SUCCEEDED(IMF2DBuffer_Lock2D(buf2d, &scan0, &pitch)) && scan0 && pitch > 0) {
            // Bytes between Y and UV planes - NV12 stores them
            // contiguously with the same pitch.
            BYTE* uv = scan0 + (size_t)pitch * (size_t)h;
            SDL_UpdateNVTexture(texture, NULL,
                                scan0, pitch,
                                uv,    pitch);
            IMF2DBuffer_Unlock2D(buf2d);
            ok = 1;
        }
        IMF2DBuffer_Release(buf2d);
        return ok;
    }

    // Fallback: contiguous flat buffer (rare for NV12 on Windows).
    BYTE* data = NULL;
    DWORD curLen = 0;
    if (FAILED(IMFMediaBuffer_Lock(buf, &data, NULL, &curLen))) return 0;
    int ok = 0;
    // For NV12 with tight packing: total = w*h*3/2. Validate before
    // forwarding to SDL to avoid out-of-bounds reads on weird buffers.
    if (data && curLen >= (DWORD)(w * h * 3 / 2)) {
        SDL_UpdateNVTexture(texture, NULL,
                            data,             w,            // Y plane
                            data + w * h,     w);           // UV plane
        ok = 1;
    }
    IMFMediaBuffer_Unlock(buf);
    return ok;
}

// Decodes video samples until we find one that matches the current
// playback clock. Uploads the latest matching frame into v->texture.
// Returns 1 if v->texture was (re)populated this call.
static int PumpVideo(UIVideo* v, SDL_Renderer* renderer) {
    if (!v->hasVideo) return 0;

    if (!v->texture) {
        // NV12 texture - SDL handles YUV->RGB on the GPU. NV12 requires
        // even width/height (chroma is subsampled 2x); the codec
        // already enforces this, but we round up defensively.
        const int texW = (v->videoWidth  + 1) & ~1;
        const int texH = (v->videoHeight + 1) & ~1;
        v->texture = SDL_CreateTexture(renderer,
                                       SDL_PIXELFORMAT_NV12,
                                       SDL_TEXTUREACCESS_STREAMING,
                                       texW, texH);
        if (!v->texture) {
            UI_ERROR(UI_CAT_VIDEO, "SDL_CreateTexture(NV12) failed: %s", SDL_GetError());
            return 0;
        }
        SDL_SetTextureScaleMode(v->texture, SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(v->texture, SDL_BLENDMODE_NONE);
    }

    const double clock = v->clockSec;
    int uploaded = 0;

    // SDL_UpdateNVTexture writes straight into GPU memory - no CPU
    // pixel buffer to manage. We just hold the next IMFSample pointer
    // as `pendingSample` until its timestamp matches the clock.
    for (int iter = 0; iter < 4; iter++) {
        // Promote pending sample whose timestamp has arrived.
        if (v->pendingFrameValid) {
            if (v->pendingFrameTime > clock + 0.001) break; // future frame
            IMFSample* held = v->pendingSample;
            v->pendingSample     = NULL;
            v->pendingFrameValid = 0;

            IMFMediaBuffer* hbuf = NULL;
            if (held && SUCCEEDED(IMFSample_ConvertToContiguousBuffer(held, &hbuf))) {
                if (UploadNV12Frame(hbuf, v->texture,
                                    v->videoWidth, v->videoHeight)) {
                    v->currentFrameTime = v->pendingFrameTime;
                    uploaded = 1;
                }
                IMFMediaBuffer_Release(hbuf);
            }
            if (held) IMFSample_Release(held);
        }

        // Read the next sample.
        IMFSample* s = NULL;
        LONGLONG ts100 = 0;
        int eof = 0;
        if (!ReadOneSample(v->reader,
                           (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                           &s, &ts100, &eof)) {
            if (eof) {
                v->eof = 1;
                if (v->loop) {
                    PROPVARIANT pv = {0};
                    pv.vt = VT_I8; pv.hVal.QuadPart = 0;
                    IMFSourceReader_SetCurrentPosition(v->reader, &GUID_NULL, &pv);
                    v->eof = 0;
                    v->clockSec = 0.0;
                    v->currentFrameTime = -1.0;
                    if (v->pendingSample) {
                        IMFSample_Release(v->pendingSample);
                        v->pendingSample = NULL;
                    }
                    v->pendingFrameValid = 0;
                } else if (v->onEnded) {
                    v->onEnded(v, v->userdata);
                }
            }
            break;
        }
        // Stash for the next iteration.
        v->pendingSample      = s;
        v->pendingFrameTime   = (double)ts100 / 10000000.0;
        v->pendingFrameValid  = 1;
    }
    return uploaded;
}

// Called by window.c each render frame for every UIVideo widget.
void UIVideo_Tick(UIVideo* v, SDL_Renderer* renderer) {
    if (!v || !v->playing) return;

    const Uint64 now = SDL_GetTicks();
    const double dt  = (v->lastTickMs == 0) ? 0.0 : (double)(now - v->lastTickMs) / 1000.0;
    v->lastTickMs = now;
    v->clockSec += dt;

    PumpAudio(v);
    PumpVideo(v, renderer);
}

// Used by the render branch to grab a ready-to-display texture.
SDL_Texture* UIVideo_GetTexture(UIVideo* v) {
    return v ? v->texture : NULL;
}

#elif defined(__linux__) && defined(MOCIDA_HAS_GSTREAMER)

// =====================================================================
// Linux backend — GStreamer playbin.
//
// Pipeline shape:
//
//   filesrc (via playbin uri)
//        |
//      decodebin (auto codec)
//        |--video--> videoconvert ---> appsink {BGRA}  -> SDL_Texture
//        |--audio--> audioconvert ---> appsink {S16LE} -> SDL_AudioStream
//
// `playbin` assembles all of that for us; we only attach the two
// appsinks via the `video-sink` / `audio-sink` properties.
//
// Threading: GStreamer drives decode on its own thread, so audio stays
// glitch-free even if the GUI tick is slow. We poll the appsinks
// non-blocking from UIVideo_Tick (which runs on the SDL thread); the
// video sink is configured with max-buffers=1 + drop=true so the
// renderer always shows the most recent decoded frame and never queues
// stale frames on the GPU.
//
// Sync: appsinks have sync=TRUE, so GStreamer's internal clock paces
// audio + video to the file's PTS. Our render loop just consumes
// whatever has expired.
// =====================================================================

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <SDL3/SDL.h>
#include <limits.h>          // PATH_MAX

struct UIVideo {
    const char* __widget_type;

    // ----- GStreamer pipeline -----
    GstElement* pipeline;        // playbin element
    GstElement* videoSink;       // appsink for decoded BGRA frames
    GstElement* audioSink;       // appsink for decoded PCM (S16LE)
    GstBus*     bus;             // for EOS/error messages
    char*       source;          // owned heap copy of the resolved path

    // ----- SDL sinks -----
    SDL_Texture*     texture;    // created lazily on first Tick once
                                 // width/height are known
    SDL_AudioStream* audio;      // created lazily on first audio sample

    // ----- Stream info (populated during preroll) -----
    int videoWidth;
    int videoHeight;
    int hasVideo;
    int hasAudio;
    int audioChannels;
    int audioSampleRate;
    double durationSec;

    // ----- Playback state -----
    int      playing;
    int      loop;
    int      muted;
    int      eof;
    float    volume;
    UIFillMode fillMode;

    // ----- Callbacks -----
    UIVideoCallback onEnded;
    void*           userdata;
};

// One-time GStreamer init. Safe to call multiple times — gst_init is
// idempotent — but a guard keeps the cost down.
//
// Why we use a constructor instead of lazy init at UIVideo_Create:
// SDL_Init on Linux loads Wayland / EGL / X11 backends that touch
// GLib's type system in a way that leaves GStreamer's later
// gst_init_check crashing inside g_option_context_parse →
// g_object_ref_sink(NULL). Confirmed with a 10-line repro: SDL_Init
// followed by gst_init_check segfaults; reversed order works. By
// scheduling gst_init via __attribute__((constructor)) we get
// to it before main() even starts, well before the host app's
// SDL_Init.
//
// We don't pass NULL/NULL for argc/argv either: GStreamer 1.28's
// internal option-context-parse dereferences a NULL option group
// when both are NULL. A minimal dummy argv (just the program name)
// routes through the safe code path.
static int g_gstInited = 0;

static void DoGstInit(void) {
    if (g_gstInited) return;
    int     fake_argc   = 1;
    char    progName[]  = "mocida";
    char*   fake_argv[] = { progName, NULL };
    char**  argv_p      = fake_argv;
    GError* err = NULL;
    if (!gst_init_check(&fake_argc, &argv_p, &err)) {
        // Can't UI_ERROR here from a constructor — the debug
        // subsystem may not be ready yet. Stderr is always safe.
        fprintf(stderr, "[mocida] gst_init failed: %s\n",
                err ? err->message : "?");
        if (err) g_error_free(err);
        return;
    }
    g_gstInited = 1;
}

static void EnsureGstInit(void) {
    DoGstInit();
}

// Public early-init hook. The host application calls this at the very
// top of main(), BEFORE SDL_Init / UIApp_Create / any other mocida API.
// On some Linux configs (notably WSLg + SDL3 + gst-plugins-base 1.28),
// calling gst_init_check AFTER SDL/Wayland have loaded segfaults inside
// g_option_context_parse. Running it first sidesteps that interaction.
//
// Safe to call zero or many times; we no-op after the first success.
void UIVideo_PreInit(void) {
    DoGstInit();
}

// Build a `file://` URI from a user-supplied path. The path is made
// absolute via realpath() because GStreamer's playbin only accepts
// absolute file URIs reliably.
static char* BuildFileUri(const char* path) {
    if (!path || !*path) return NULL;
    char abs[PATH_MAX];
    if (path[0] == '/') {
        // already absolute, but realpath canonicalises and verifies
        if (!realpath(path, abs)) return NULL;
    } else {
        if (!realpath(path, abs)) return NULL;
    }
    char* uri = gst_filename_to_uri(abs, NULL);
    return uri; // caller g_free()s
}

// Pulls a video preroll sample (without blocking the pipeline) to read
// width/height from its caps. Called once after going to PAUSED.
static void ReadVideoCaps(UIVideo* v) {
    GstSample* sample = gst_app_sink_try_pull_preroll(
        GST_APP_SINK(v->videoSink), 0);
    if (!sample) return;
    GstCaps* caps = gst_sample_get_caps(sample);
    if (caps && gst_caps_get_size(caps) > 0) {
        GstStructure* s = gst_caps_get_structure(caps, 0);
        gst_structure_get_int(s, "width",  &v->videoWidth);
        gst_structure_get_int(s, "height", &v->videoHeight);
        v->hasVideo = (v->videoWidth > 0 && v->videoHeight > 0);
    }
    gst_sample_unref(sample);
}

// Same for audio: channel count + sample rate.
static void ReadAudioCaps(UIVideo* v) {
    GstSample* sample = gst_app_sink_try_pull_preroll(
        GST_APP_SINK(v->audioSink), 0);
    if (!sample) return;
    GstCaps* caps = gst_sample_get_caps(sample);
    if (caps && gst_caps_get_size(caps) > 0) {
        GstStructure* s = gst_caps_get_structure(caps, 0);
        gst_structure_get_int(s, "channels", &v->audioChannels);
        gst_structure_get_int(s, "rate",     &v->audioSampleRate);
        v->hasAudio = (v->audioChannels > 0 && v->audioSampleRate > 0);
    }
    gst_sample_unref(sample);
}

UIVideo* UIVideo_Create(const char* path) {
    if (!path) return NULL;
    UI_INFO(UI_CAT_VIDEO, "UIVideo_Create: path=%s", path);
    EnsureGstInit();
    if (!g_gstInited) { UI_ERROR(UI_CAT_VIDEO, "gst init failed"); return NULL; }
    UI_INFO(UI_CAT_VIDEO, "UIVideo_Create: gst init ok");

    UIVideo* v = (UIVideo*)calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->__widget_type = UI_WIDGET_VIDEO;
    v->volume   = 1.0f;
    v->fillMode = FILL_FIT;

    char* uri = BuildFileUri(path);
    if (!uri) {
        UI_ERROR(UI_CAT_VIDEO, "video file not found or path unresolvable: %s", path);
        free(v);
        return NULL;
    }
    UI_INFO(UI_CAT_VIDEO, "UIVideo_Create: uri=%s", uri);
    v->source = strdup(path);

    v->pipeline = gst_element_factory_make("playbin", "mocida-playbin");
    UI_INFO(UI_CAT_VIDEO, "UIVideo_Create: playbin=%p", (void*)v->pipeline);
    if (!v->pipeline) {
        UI_ERROR(UI_CAT_VIDEO, "playbin element missing — "
                 "install gstreamer1.0-plugins-base");
        g_free(uri); free(v->source); free(v);
        return NULL;
    }

    // Both sinks come from the `app` element in gst-plugins-base. If
    // gstreamer1.0-plugins-base isn't installed the factory_make
    // returns NULL and we'd crash below in g_object_set when playbin
    // tries to g_object_ref_sink a NULL pointer. Bail loudly instead.
    v->videoSink = gst_element_factory_make("appsink", "mocida-video-sink");
    v->audioSink = gst_element_factory_make("appsink", "mocida-audio-sink");
    UI_INFO(UI_CAT_VIDEO, "UIVideo_Create: videoSink=%p audioSink=%p",
            (void*)v->videoSink, (void*)v->audioSink);
    if (!v->videoSink || !v->audioSink) {
        UI_ERROR(UI_CAT_VIDEO, "appsink element missing — "
                 "install gstreamer1.0-plugins-base (provides appsink + playbin)");
        if (v->videoSink) gst_object_unref(v->videoSink);
        if (v->audioSink) gst_object_unref(v->audioSink);
        gst_object_unref(v->pipeline);
        g_free(uri); free(v->source); free(v);
        return NULL;
    }

    // Video sink: BGRA, drop-old. SDL_PIXELFORMAT_BGRA32 matches
    // GStreamer's "BGRA" caps on little-endian, so SDL_UpdateTexture
    // can do a straight memcpy with no per-pixel swizzle.
    GstCaps* vcaps = gst_caps_new_simple("video/x-raw",
        "format", G_TYPE_STRING, "BGRA",
        NULL);
    gst_app_sink_set_caps(GST_APP_SINK(v->videoSink), vcaps);
    gst_caps_unref(vcaps);
    g_object_set(v->videoSink,
        "emit-signals", FALSE,        // pull instead of signal
        "max-buffers",  (guint)1,     // only keep latest frame
        "drop",         TRUE,         // drop overruns silently
        "sync",         TRUE,         // honour gst clock
        NULL);

    // Audio sink: S16LE interleaved.
    GstCaps* acaps = gst_caps_new_simple("audio/x-raw",
        "format",   G_TYPE_STRING, "S16LE",
        "layout",   G_TYPE_STRING, "interleaved",
        NULL);
    gst_app_sink_set_caps(GST_APP_SINK(v->audioSink), acaps);
    gst_caps_unref(acaps);
    g_object_set(v->audioSink,
        "emit-signals", FALSE,
        "max-buffers",  (guint)8,     // small queue for buffering
        "drop",         FALSE,        // never drop audio
        "sync",         TRUE,
        NULL);

    g_object_set(v->pipeline,
        "uri",         uri,
        "video-sink",  v->videoSink,
        "audio-sink",  v->audioSink,
        NULL);
    g_free(uri);

    v->bus = gst_element_get_bus(v->pipeline);

    // Preroll to PAUSED so caps + duration become queryable. A blocking
    // get_state with a 5s ceiling waits for the initial decode probe.
    GstStateChangeReturn r = gst_element_set_state(v->pipeline, GST_STATE_PAUSED);
    if (r == GST_STATE_CHANGE_FAILURE) {
        UI_ERROR(UI_CAT_VIDEO, "failed to open video: %s", path);
        UIVideo_Destroy(v);
        return NULL;
    }
    GstState st = GST_STATE_NULL;
    gst_element_get_state(v->pipeline, &st, NULL, 5 * GST_SECOND);
    if (st != GST_STATE_PAUSED) {
        UI_ERROR(UI_CAT_VIDEO, "preroll timeout / failure for: %s", path);
        UIVideo_Destroy(v);
        return NULL;
    }

    gint64 dur = 0;
    if (gst_element_query_duration(v->pipeline, GST_FORMAT_TIME, &dur)) {
        v->durationSec = (double)dur / (double)GST_SECOND;
    }

    ReadVideoCaps(v);
    ReadAudioCaps(v);

    return v;
}

UIVideo* UIVideo_Play(UIVideo* v) {
    if (!v || !v->pipeline) return v;
    gst_element_set_state(v->pipeline, GST_STATE_PLAYING);
    v->playing = 1;
    v->eof = 0;
    return v;
}

UIVideo* UIVideo_Pause(UIVideo* v) {
    if (!v || !v->pipeline) return v;
    gst_element_set_state(v->pipeline, GST_STATE_PAUSED);
    v->playing = 0;
    return v;
}

UIVideo* UIVideo_Stop(UIVideo* v) {
    if (!v || !v->pipeline) return v;
    UIVideo_Seek(v, 0.0);
    gst_element_set_state(v->pipeline, GST_STATE_PAUSED);
    v->playing = 0;
    return v;
}

void UIVideo_Seek(UIVideo* v, double seconds) {
    if (!v || !v->pipeline) return;
    if (seconds < 0.0) seconds = 0.0;
    gint64 ns = (gint64)(seconds * (double)GST_SECOND);
    gst_element_seek_simple(v->pipeline, GST_FORMAT_TIME,
        (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT), ns);
    v->eof = 0;
}

UIVideo* UIVideo_SetLoop(UIVideo* v, int loop) {
    if (v) v->loop = loop ? 1 : 0;
    return v;
}

UIVideo* UIVideo_SetVolume(UIVideo* v, float volume) {
    if (!v) return v;
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    v->volume = volume;
    // SDL3 mixes via per-stream gain — no need to scale samples ourselves.
    if (v->audio) SDL_SetAudioStreamGain(v->audio, v->muted ? 0.0f : v->volume);
    return v;
}

UIVideo* UIVideo_SetMuted(UIVideo* v, int muted) {
    if (!v) return v;
    v->muted = muted ? 1 : 0;
    if (v->audio) SDL_SetAudioStreamGain(v->audio, v->muted ? 0.0f : v->volume);
    return v;
}

UIVideo* UIVideo_SetFillMode(UIVideo* v, UIFillMode m) {
    if (v) v->fillMode = m;
    return v;
}

UIVideo* UIVideo_OnEnded(UIVideo* v, UIVideoCallback cb, void* userdata) {
    if (v) { v->onEnded = cb; v->userdata = userdata; }
    return v;
}

int    UIVideo_IsPlaying  (UIVideo* v) { return v && v->playing && !v->eof; }
int    UIVideo_GetWidth   (UIVideo* v) { return v ? v->videoWidth  : 0; }
int    UIVideo_GetHeight  (UIVideo* v) { return v ? v->videoHeight : 0; }
double UIVideo_GetDuration(UIVideo* v) { return v ? v->durationSec : 0.0; }

double UIVideo_GetTime(UIVideo* v) {
    if (!v || !v->pipeline) return 0.0;
    gint64 pos = 0;
    if (gst_element_query_position(v->pipeline, GST_FORMAT_TIME, &pos))
        return (double)pos / (double)GST_SECOND;
    return 0.0;
}

void UIVideo_Destroy(UIVideo* v) {
    if (!v) return;
    if (v->pipeline) {
        gst_element_set_state(v->pipeline, GST_STATE_NULL);
        gst_object_unref(v->pipeline);
    }
    if (v->bus)      gst_object_unref(v->bus);
    if (v->texture)  SDL_DestroyTexture(v->texture);
    if (v->audio)    SDL_DestroyAudioStream(v->audio);
    free(v->source);
    free(v);
}

void UIVideo_Tick(UIVideo* v, SDL_Renderer* renderer) {
    if (!v || !v->pipeline || !renderer) return;

    // Drain bus messages — EOS triggers loop/seek-0 or end callback;
    // ERROR aborts playback. pop is non-blocking.
    GstMessage* msg;
    while ((msg = gst_bus_pop(v->bus)) != NULL) {
        switch (GST_MESSAGE_TYPE(msg)) {
            case GST_MESSAGE_EOS:
                if (v->loop) {
                    UIVideo_Seek(v, 0.0);
                } else {
                    v->eof = 1;
                    v->playing = 0;
                    if (v->onEnded) v->onEnded(v, v->userdata);
                }
                break;
            case GST_MESSAGE_ERROR: {
                GError* err = NULL;
                gchar*  dbg = NULL;
                gst_message_parse_error(msg, &err, &dbg);
                UI_ERROR(UI_CAT_VIDEO, "gstreamer error: %s (%s)",
                         err ? err->message : "?",
                         dbg ? dbg : "no debug info");
                g_clear_error(&err);
                g_free(dbg);
                v->eof = 1;
                v->playing = 0;
                break;
            }
            default: break;
        }
        gst_message_unref(msg);
    }

    if (!v->hasVideo) return;

    // Pull at most one video frame per tick. With max-buffers=1 +
    // drop=true on the appsink, GStreamer already feeds us only the
    // newest expired frame, so we don't have to loop.
    GstSample* sample = gst_app_sink_try_pull_sample(
        GST_APP_SINK(v->videoSink), 0);
    if (sample) {
        GstBuffer* buf = gst_sample_get_buffer(sample);
        GstMapInfo info;
        if (buf && gst_buffer_map(buf, &info, GST_MAP_READ)) {
            if (!v->texture) {
                v->texture = SDL_CreateTexture(renderer,
                    SDL_PIXELFORMAT_BGRA32,
                    SDL_TEXTUREACCESS_STREAMING,
                    v->videoWidth, v->videoHeight);
            }
            if (v->texture) {
                SDL_UpdateTexture(v->texture, NULL, info.data,
                                  v->videoWidth * 4);
            }
            gst_buffer_unmap(buf, &info);
        }
        gst_sample_unref(sample);
    }

    // Audio: lazily open the device stream once we know the format,
    // then drain every available buffer this tick. Gain is set via
    // SDL3 per-stream so volume changes don't require re-encoding.
    if (v->hasAudio) {
        if (!v->audio) {
            SDL_AudioSpec spec;
            SDL_zero(spec);
            spec.format   = SDL_AUDIO_S16LE;
            spec.channels = v->audioChannels;
            spec.freq     = v->audioSampleRate;
            v->audio = SDL_OpenAudioDeviceStream(
                SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
            if (v->audio) {
                SDL_SetAudioStreamGain(v->audio,
                    v->muted ? 0.0f : v->volume);
                SDL_ResumeAudioStreamDevice(v->audio);
            }
        }
        if (v->audio) {
            while ((sample = gst_app_sink_try_pull_sample(
                        GST_APP_SINK(v->audioSink), 0)) != NULL) {
                GstBuffer* buf = gst_sample_get_buffer(sample);
                GstMapInfo info;
                if (buf && gst_buffer_map(buf, &info, GST_MAP_READ)) {
                    SDL_PutAudioStreamData(v->audio, info.data,
                                           (int)info.size);
                    gst_buffer_unmap(buf, &info);
                }
                gst_sample_unref(sample);
            }
        }
    }
}

SDL_Texture* UIVideo_GetTexture(UIVideo* v) {
    return v ? v->texture : NULL;
}

#else // !_WIN32 && !(linux + GStreamer)

// Stub backend for non-Windows builds. Lets the rest of the project
// compile; callers get a NULL UIVideo and a stderr message.

/** Non-Windows stub of UIVideo. Holds only the type tag; every method
 *  returns NULL / logs an error. */
struct UIVideo {
    const char* __widget_type; /**< Widget type tag (== UI_WIDGET_VIDEO). */
};

void UIVideo_PreInit(void) { /* nothing to do — no backend */ }

UIVideo* UIVideo_Create(const char* path) {
    (void)path;
    UI_ERROR(UI_CAT_VIDEO, "no video backend available on this build "
             "(Windows: Media Foundation; Linux: install GStreamer dev packages)");
    return NULL;
}
UIVideo* UIVideo_Play (UIVideo* v) { return v; }
UIVideo* UIVideo_Pause(UIVideo* v) { return v; }
UIVideo* UIVideo_Stop (UIVideo* v) { return v; }
UIVideo* UIVideo_SetLoop(UIVideo* v, int l){ (void)l; return v; }
UIVideo* UIVideo_SetVolume(UIVideo* v, float g){ (void)g; return v; }
UIVideo* UIVideo_SetMuted (UIVideo* v, int m){ (void)m; return v; }
UIVideo* UIVideo_SetFillMode(UIVideo* v, UIFillMode m){ (void)m; return v; }
UIVideo* UIVideo_OnEnded (UIVideo* v, UIVideoCallback c, void* u){ (void)c; (void)u; return v; }
int UIVideo_IsPlaying (UIVideo* v){ (void)v; return 0; }
int UIVideo_GetWidth  (UIVideo* v){ (void)v; return 0; }
int UIVideo_GetHeight (UIVideo* v){ (void)v; return 0; }
double UIVideo_GetTime    (UIVideo* v){ (void)v; return 0.0; }
double UIVideo_GetDuration(UIVideo* v){ (void)v; return 0.0; }
void   UIVideo_Seek       (UIVideo* v, double s){ (void)v; (void)s; }
void UIVideo_Destroy  (UIVideo* v){ free(v); }

// Render-side stubs (renderer + widget bookkeeping).
void          UIVideo_Tick(UIVideo* v, SDL_Renderer* r) { (void)v; (void)r; }
SDL_Texture*  UIVideo_GetTexture(UIVideo* v) { (void)v; return NULL; }

#endif

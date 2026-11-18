#ifndef UIKIT_NET_H
#define UIKIT_NET_H

/**
 * HTTP for games and tools. Downloads update a progress value the caller
 * can poll while the transfer runs on this thread.
 */

/** 1 when `url` answers. A short connect, not a full download. */
int UINet_Reachable(const char* url);

/**
 * GET `url` into a freshly allocated buffer. Caller frees `*out_data`
 * with SDL_free. Returns 1 on HTTP 2xx.
 */
int UINet_Get(const char* url, void** out_data, int* out_size);

/**
 * Saves `url` to `path`, creating parent folders as needed.
 * `UINet_Progress` moves from 0 to 1 while this runs.
 * Returns 1 when the file is complete.
 */
int UINet_Download(const char* url, const char* path);

/** 0..1 for the download in progress. 1 when idle after success. */
float UINet_Progress(void);

/** Bytes received by the current or last download. */
int UINet_BytesDone(void);

/** Total bytes if the server sent a length, else 0. */
int UINet_BytesTotal(void);

/** Last error string, or "". */
const char* UINet_Error(void);

#endif

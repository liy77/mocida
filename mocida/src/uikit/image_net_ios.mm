// iOS remote-image HTTP backend.
//
// libcurl is not part of the iOS build (see CMakeLists — `find_package(CURL)`
// is skipped on iOS), so UIImage's remote-source loader fetches the bytes via
// NSURLSession here instead. The function is plain-C-callable and is reached
// from image.c through an `extern` declaration on iOS.
//
// It runs on mocida's image-download worker thread (UIImage spawns one per
// remote image), so blocking on a semaphore until the request finishes is
// fine — it never stalls the UI thread.

#import <Foundation/Foundation.h>
#include <stdlib.h>
#include <string.h>

// Returns 1 on success, with a malloc'd copy of the body in *outData
// (caller owns it) and its length in *outSize. Returns 0 on any failure
// (bad URL, network error, HTTP >= 400, empty body, OOM).
//
// extern "C": compiled as Objective-C++ but called from image.c (C), so the
// symbol must keep C linkage (no name mangling).
extern "C" int UIImage_RemoteHttpDownload_iOS(const char* url,
                                              void** outData, size_t* outSize) {
    if (outData) *outData = NULL;
    if (outSize) *outSize = 0;
    if (!url || !outData || !outSize) return 0;

    @autoreleasepool {
        NSString* str = [NSString stringWithUTF8String:url];
        NSURL* nsurl = str ? [NSURL URLWithString:str] : nil;
        if (!nsurl) return 0;

        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        // Written inside the completion block, read after the wait. The
        // semaphore provides the happens-before barrier between the two.
        __block void*  buf = NULL;
        __block size_t len = 0;

        NSURLSessionDataTask* task = [[NSURLSession sharedSession]
            dataTaskWithURL:nsurl
            completionHandler:^(NSData* data, NSURLResponse* resp, NSError* err) {
                long status = 0;
                if ([resp isKindOfClass:[NSHTTPURLResponse class]]) {
                    status = (long)[(NSHTTPURLResponse*)resp statusCode];
                }
                if (!err && data && data.length > 0 && status < 400) {
                    void* b = malloc(data.length);
                    if (b) {
                        memcpy(b, data.bytes, data.length);
                        buf = b;
                        len = (size_t)data.length;
                    }
                }
                dispatch_semaphore_signal(sem);
            }];
        [task resume];
        dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);

        if (!buf) return 0;
        *outData = buf;
        *outSize = len;
        return 1;
    }
}

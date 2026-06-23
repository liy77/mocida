// test_device_simulator.c — smoke test for the device simulator API.

#include <uikit/app.h>
#include <uikit/device_simulator.h>
#include <uikit/widget.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int roundtrip_ok(UIApp* app, float dx, float dy) {
    float wx = 0, wy = 0;
    UIApp_DeviceSimulatorDeviceToWindow(app, dx, dy, &wx, &wy);
    float rx = 0, ry = 0;
    UIApp_DeviceSimulatorWindowToDevice(app, wx, wy, &rx, &ry);
    return fabsf(rx - dx) <= 1.0f && fabsf(ry - dy) <= 1.0f;
}

int main(void) {
    UIApp* app = UIApp_Create("Device Sim Test", 200, 200);
    if (!app) { fprintf(stderr, "failed to create app\n"); return 1; }

    UIDeviceProfile profile = {
        .name = "testphone",
        .width = 393, .height = 852,
        .safeTop = 59, .safeBottom = 34,
        .safeLeft = 0, .safeRight = 0,
        .scale = 3.0f,
        .frameScreenX = 20, .frameScreenY = 70,
        .frameScreenW = 393, .frameScreenH = 852,
        .frameW = 433, .frameH = 972
    };

    UIApp_EnableDeviceSimulator(app, &profile, NULL, 1.0f, UI_DEVICE_ORIENTATION_PORTRAIT);
    UIApp_SetWindowSize(app, profile.frameW, profile.frameH);
    if (!UIApp_IsDeviceSimulatorEnabled(app)) {
        fprintf(stderr, "simulator not enabled\n");
        UIApp_Destroy(app);
        return 1;
    }

    printf("device size: %dx%d\n", UIApp_GetWidth(app), UIApp_GetHeight(app));
    if (UIApp_GetWidth(app) != 393 || UIApp_GetHeight(app) != 852) {
        fprintf(stderr, "unexpected device size\n");
        UIApp_Destroy(app);
        return 1;
    }

    const UIDeviceProfile* p = UIApp_GetCurrentDeviceProfile(app);
    if (!p || p->width != 393) {
        fprintf(stderr, "profile mismatch\n");
        UIApp_Destroy(app);
        return 1;
    }

    if (!roundtrip_ok(app, 0.0f, 0.0f) || !roundtrip_ok(app, 100.0f, 200.0f)) {
        fprintf(stderr, "portrait round-trip failed\n");
        UIApp_Destroy(app);
        return 1;
    }

    UIApp_SetDeviceOrientation(app, UI_DEVICE_ORIENTATION_LANDSCAPE);
    UIApp_SetWindowSize(app, profile.frameH, profile.frameW);
    if (UIApp_GetCurrentDeviceOrientation(app) != UI_DEVICE_ORIENTATION_LANDSCAPE) {
        fprintf(stderr, "orientation did not change\n");
        UIApp_Destroy(app);
        return 1;
    }
    if (UIApp_GetWidth(app) != 852 || UIApp_GetHeight(app) != 393) {
        fprintf(stderr, "landscape size wrong\n");
        UIApp_Destroy(app);
        return 1;
    }

    if (!roundtrip_ok(app, 0.0f, 0.0f) || !roundtrip_ok(app, 100.0f, 200.0f)) {
        fprintf(stderr, "landscape round-trip failed\n");
        UIApp_Destroy(app);
        return 1;
    }

    UIApp_SetDeviceOrientation(app, UI_DEVICE_ORIENTATION_PORTRAIT);
    UIApp_SetWindowSize(app, profile.frameW, profile.frameH);
    if (UIApp_GetCurrentDeviceOrientation(app) != UI_DEVICE_ORIENTATION_PORTRAIT) {
        fprintf(stderr, "orientation did not reset\n");
        UIApp_Destroy(app);
        return 1;
    }

    if (!roundtrip_ok(app, 0.0f, 0.0f) || !roundtrip_ok(app, 100.0f, 200.0f)) {
        fprintf(stderr, "portrait reset round-trip failed\n");
        UIApp_Destroy(app);
        return 1;
    }

    UIApp_DisableDeviceSimulator(app);
    if (UIApp_IsDeviceSimulatorEnabled(app)) {
        fprintf(stderr, "simulator still enabled after disable\n");
        UIApp_Destroy(app);
        return 1;
    }

    UIApp_EnableDeviceSimulator(app, &profile, NULL, 1.0f, UI_DEVICE_ORIENTATION_PORTRAIT);
    if (!UIApp_IsDeviceSimulatorEnabled(app)) {
        fprintf(stderr, "simulator not re-enabled\n");
        UIApp_Destroy(app);
        return 1;
    }

    UIApp_Destroy(app);
    printf("OK\n");
    return 0;
}

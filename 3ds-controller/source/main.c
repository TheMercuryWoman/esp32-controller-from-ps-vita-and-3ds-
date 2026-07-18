/*
 * 3DS Controller -> ESP32 Bridge
 * IP address is entered using the 3DS's system software keyboard,
 * so it can be changed without recompiling.
 */

#include <citro2d.h>
#include <3ds.h>
#include <stdio.h>
#include <string.h>

static int g_ip[4] = {192, 168, 1, 187};
static char g_ip_str[16];

static volatile u32 g_kHeld = 0;
static volatile int g_cpad_x = 0, g_cpad_y = 0;
static volatile int g_last_send_ok = 0;
static volatile int g_running = 1;
static volatile int g_net_ready = 0;

int send_command(const char* url) {
    Result ret;
    httpcContext context;

    ret = httpcOpenContext(&context, HTTPC_METHOD_GET, (char*)url, 0);
    if (ret != 0) return 0;

    httpcSetSSLOpt(&context, SSLCOPT_DisableVerify);
    httpcAddRequestHeaderField(&context, (char*)"User-Agent", (char*)"3DSController/1.0");

    ret = httpcBeginRequest(&context);
    if (ret != 0) {
        httpcCloseContext(&context);
        return 0;
    }

    u32 statuscode = 0;
    ret = httpcGetResponseStatusCode(&context, &statuscode);

    httpcCloseContext(&context);
    return (ret == 0 && statuscode == 200);
}

void network_thread(void *arg) {
    while (g_running) {
        if (g_net_ready) {
            char url[256];
            snprintf(url, sizeof(url),
                "http://%s/controller?btn=%lu&lx=%d&ly=%d&dev=3ds",
                g_ip_str, (unsigned long)g_kHeld, g_cpad_x, g_cpad_y);
            g_last_send_ok = send_command(url);
        }
        svcSleepThread(50 * 1000 * 1000); // 50ms -> ~20 sends/sec
    }
    threadExit(0);
}

int main(int argc, char* argv[]) {
    gfxInitDefault();
    httpcInit(0);

    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    consoleInit(GFX_BOTTOM, NULL);

    C3D_RenderTarget* top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);

    // ---- IP entry via the system's software keyboard ----
    // swkbdInputText blocks and runs its own UI until the user confirms/cancels,
    // so unlike the Vita's IME dialog, no manual per-frame pumping is needed here.
    char ip_input[32] = "192.168.1.187";
    SwkbdState swkbd;
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 1, 20);
    swkbdSetHintText(&swkbd, "e.g. 192.168.1.187");
    swkbdSetInitialText(&swkbd, ip_input);
    swkbdSetValidation(&swkbd, SWKBD_ANYTHING, 0, 0);

    SwkbdButton button = swkbdInputText(&swkbd, ip_input, sizeof(ip_input));

    if (button != SWKBD_BUTTON_NONE) {
        int a, b, c, d;
        if (sscanf(ip_input, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
            g_ip[0] = a & 0xFF; g_ip[1] = b & 0xFF; g_ip[2] = c & 0xFF; g_ip[3] = d & 0xFF;
        }
    }

    snprintf(g_ip_str, sizeof(g_ip_str), "%d.%d.%d.%d", g_ip[0], g_ip[1], g_ip[2], g_ip[3]);
    g_net_ready = 1; // httpc service was already initialized at startup

    s32 main_prio = 0x30;
    svcGetThreadPriority(&main_prio, CUR_THREAD_HANDLE);
    threadCreate(network_thread, NULL, 0x8000, main_prio - 1, -1, false);

    // ---- Main controller screen ----
    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();
        if (kDown & KEY_START) break;

        circlePosition cpad;
        hidCircleRead(&cpad);

        g_kHeld = kHeld;
        g_cpad_x = cpad.dx;
        g_cpad_y = cpad.dy;

        printf("\x1b[1;1H3DS -> ESP32 Controller Bridge   \n");
        printf("\x1b[2;1HTarget: %s          \n", g_ip_str);
        printf("\x1b[3;1HButtons held: 0x%08lX      \n", (unsigned long)kHeld);
        printf("\x1b[4;1HCircle Pad: X=%4d Y=%4d      \n", cpad.dx, cpad.dy);
        printf("\x1b[5;1HLast send: %s          \n", g_last_send_ok ? "OK" : "FAILED / waiting");
        printf("\x1b[7;1HPress START to quit          \n");

        int w = 60 + (cpad.dx / 4);
        int h = 60 + (cpad.dy / 4);
        if (w < 10) w = 10;
        if (h < 10) h = 10;

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(top, C2D_Color32(0x18, 0x18, 0x18, 0xFF));
        C2D_SceneBegin(top);
        u32 color = g_last_send_ok ? C2D_Color32(80, 220, 80, 255) : C2D_Color32(220, 80, 80, 255);
        C2D_DrawRectSolid(160 - w/2, 100 - h/2, 0, w, h, color);
        C3D_FrameEnd(0);
    }

    g_running = 0;
    svcSleepThread(200 * 1000 * 1000);

    C2D_Fini();
    C3D_Fini();
    httpcExit();
    gfxExit();
    return 0;
}

/*
 * Vita Controller -> ESP32 Bridge
 * IP address is entered on-device with the D-pad (no recompiling needed
 * when the ESP32's IP changes) then streams button/stick state over HTTP.
 */

#include <vita2d.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/ctrl.h>
#include <psp2/sysmodule.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/net/http.h>
#include <stdio.h>
#include <string.h>

#define NET_INIT_SIZE (1 * 1024 * 1024)
static char net_memory[NET_INIT_SIZE];
static int http_tmpl_id = -1;

// Editable at runtime -- pre-filled with the last known-good address as a default.
static int g_ip[4] = {192, 168, 1, 187};
static char g_ip_str[16];

static volatile unsigned int g_buttons = 0;
static volatile int g_lx = 128, g_ly = 128, g_rx = 128, g_ry = 128;
static volatile int g_net_ready = 0;
static volatile int g_last_send_ok = 0;
static volatile int g_running = 1;

int net_init(void) {
    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    SceNetInitParam p;
    p.memory = net_memory; p.size = NET_INIT_SIZE; p.flags = 0;
    int ret = sceNetInit(&p);
    if (ret < 0 && ret != SCE_NET_ERROR_EBUSY) return 0;
    sceNetCtlInit();

    sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP);
    ret = sceHttpInit(1 * 1024 * 1024);
    if (ret < 0) return 0;

    http_tmpl_id = sceHttpCreateTemplate("VitaController/1.0", SCE_HTTP_VERSION_1_1, SCE_TRUE);
    if (http_tmpl_id < 0) return 0;

    sceHttpSetConnectTimeOut(http_tmpl_id, 500000);
    sceHttpSetSendTimeOut(http_tmpl_id, 500000);
    sceHttpSetRecvTimeOut(http_tmpl_id, 500000);
    return 1;
}

void net_term(void) {
    if (http_tmpl_id >= 0) sceHttpDeleteTemplate(http_tmpl_id);
    sceHttpTerm();
    sceSysmoduleUnloadModule(SCE_SYSMODULE_HTTP);
    sceNetCtlTerm();
    sceNetTerm();
    sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
}

int send_command(const char* url) {
    if (http_tmpl_id < 0) return 0;
    int conn_id = sceHttpCreateConnectionWithURL(http_tmpl_id, url, SCE_FALSE);
    if (conn_id < 0) return 0;
    int req_id = sceHttpCreateRequestWithURL(conn_id, SCE_HTTP_METHOD_GET, url, 0);
    if (req_id < 0) { sceHttpDeleteConnection(conn_id); return 0; }
    int ret = sceHttpSendRequest(req_id, NULL, 0);
    sceHttpDeleteRequest(req_id);
    sceHttpDeleteConnection(conn_id);
    return (ret >= 0);
}

int network_thread(SceSize args, void *argp) {
    while (g_running) {
        if (g_net_ready) {
            char url[256];
            snprintf(url, sizeof(url),
                "http://%s/controller?btn=%u&lx=%d&ly=%d&rx=%d&ry=%d&dev=vita",
                g_ip_str, g_buttons, g_lx, g_ly, g_rx, g_ry);
            g_last_send_ok = send_command(url);
        }
        sceKernelDelayThread(50 * 1000);
    }
    return 0;
}

int main(void) {
    vita2d_init();
    vita2d_set_clear_color(RGBA8(0x18, 0x18, 0x18, 0xFF));

    sceSysmoduleLoadModule(SCE_SYSMODULE_PGF);
    vita2d_pgf *font = vita2d_load_default_pgf();

    SceCtrlData pad, prev_pad;
    memset(&pad, 0, sizeof(pad));
    memset(&prev_pad, 0, sizeof(prev_pad));
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    // ---- IP entry screen ----
    int selected_octet = 0;
    int editing = 1;

    while (editing) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        unsigned int pressed = pad.buttons & ~prev_pad.buttons; // newly pressed this frame
        prev_pad = pad;

        if (pressed & SCE_CTRL_LEFT)     selected_octet = (selected_octet + 3) % 4;
        if (pressed & SCE_CTRL_RIGHT)    selected_octet = (selected_octet + 1) % 4;
        if (pressed & SCE_CTRL_UP)       g_ip[selected_octet] = (g_ip[selected_octet] + 1) % 256;
        if (pressed & SCE_CTRL_DOWN)     g_ip[selected_octet] = (g_ip[selected_octet] + 255) % 256;
        if (pressed & SCE_CTRL_LTRIGGER) g_ip[selected_octet] = (g_ip[selected_octet] + 246) % 256; // -10
        if (pressed & SCE_CTRL_RTRIGGER) g_ip[selected_octet] = (g_ip[selected_octet] + 10) % 256;
        if (pressed & SCE_CTRL_CROSS)    editing = 0;

        vita2d_start_drawing();
        vita2d_clear_screen();

        if (font) {
            vita2d_pgf_draw_text(font, 20, 40, RGBA8(255,255,255,255), 1.2f, "Enter ESP32 IP Address");
            vita2d_pgf_draw_text(font, 20, 90, RGBA8(200,200,200,255), 1.0f, "D-Pad Left/Right: select octet");
            vita2d_pgf_draw_text(font, 20, 115, RGBA8(200,200,200,255), 1.0f, "D-Pad Up/Down: +/-1    L/R: +/-10");
            vita2d_pgf_draw_text(font, 20, 140, RGBA8(200,200,200,255), 1.0f, "Press X to confirm and connect");

            char line[16];
            int x = 40;
            for (int i = 0; i < 4; i++) {
                snprintf(line, sizeof(line), "%3d", g_ip[i]);
                unsigned int color = (i == selected_octet) ? RGBA8(80,220,80,255) : RGBA8(255,255,255,255);
                vita2d_pgf_draw_text(font, x, 220, color, 2.0f, line);
                x += 70;
                if (i < 3) {
                    vita2d_pgf_draw_text(font, x, 220, RGBA8(255,255,255,255), 2.0f, ".");
                    x += 25;
                }
            }
        }

        vita2d_end_drawing();
        vita2d_swap_buffers();
        sceKernelDelayThread(16 * 1000);
    }

    snprintf(g_ip_str, sizeof(g_ip_str), "%d.%d.%d.%d", g_ip[0], g_ip[1], g_ip[2], g_ip[3]);

    g_net_ready = net_init();
    SceUID net_thid = sceKernelCreateThread("network_thread", network_thread,
        0x10000100, 0x10000, 0, 0, NULL);
    if (net_thid >= 0) sceKernelStartThread(net_thid, 0, NULL);

    // ---- Main controller screen ----
    while (1) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        if ((pad.buttons & SCE_CTRL_START) && (pad.buttons & SCE_CTRL_TRIANGLE)) break;

        g_buttons = pad.buttons;
        g_lx = pad.lx; g_ly = pad.ly; g_rx = pad.rx; g_ry = pad.ry;

        vita2d_start_drawing();
        vita2d_clear_screen();

        if (font) {
            vita2d_pgf_draw_text(font, 20, 40, RGBA8(255, 255, 255, 255), 1.2f,
                "PS Vita -> ESP32 Controller Bridge");
            char line[128];
            snprintf(line, sizeof(line), "Target: %s", g_ip_str);
            vita2d_pgf_draw_text(font, 20, 80, RGBA8(200, 200, 200, 255), 1.0f, line);
            vita2d_pgf_draw_text(font, 20, 110,
                g_net_ready ? RGBA8(80, 220, 80, 255) : RGBA8(220, 80, 80, 255),
                1.0f, g_net_ready ? "Network: READY" : "Network: FAILED TO INIT");
            vita2d_pgf_draw_text(font, 20, 140,
                g_last_send_ok ? RGBA8(80, 220, 80, 255) : RGBA8(220, 220, 80, 255),
                1.0f, g_last_send_ok ? "Last send: OK" : "Last send: FAILED / waiting");
            snprintf(line, sizeof(line), "Buttons (raw): 0x%08X", (unsigned int)pad.buttons);
            vita2d_pgf_draw_text(font, 20, 190, RGBA8(255, 255, 255, 255), 1.0f, line);
            snprintf(line, sizeof(line), "Left Stick:  X=%3d  Y=%3d", pad.lx, pad.ly);
            vita2d_pgf_draw_text(font, 20, 220, RGBA8(255, 255, 255, 255), 1.0f, line);
            snprintf(line, sizeof(line), "Right Stick: X=%3d  Y=%3d", pad.rx, pad.ry);
            vita2d_pgf_draw_text(font, 20, 250, RGBA8(255, 255, 255, 255), 1.0f, line);
            vita2d_pgf_draw_text(font, 20, 500, RGBA8(150, 150, 150, 255), 0.8f,
                "Hold START + TRIANGLE to quit");
        }

        vita2d_end_drawing();
        vita2d_swap_buffers();
        sceKernelDelayThread(16 * 1000);
    }

    g_running = 0;
    sceKernelDelayThread(200 * 1000);
    net_term();
    if (font) vita2d_free_pgf(font);
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}

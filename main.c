
#include <pspkernel.h>
#include <pspctrl.h>
#include <pspnet_inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdint.h>
#include <string.h>

#include "hooking.h"
#include "disc_ident.h"

#define UDP_LISTEN_PORT 42069

/* Nilai tombol dipertahankan persis seperti source Folax. */
#define PSP_CTRL_CROSS     0x00000040
#define PSP_CTRL_SQUARE    0x00000080

#define GAME_BUTTON_CACHE  0x08A12C00
#define GAME_INPUT_BASE    0x08A00000

PSP_MODULE_INFO("GTRemasteredUDP", PSP_MODULE_KERNEL, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);

typedef struct {
    int8_t tiltX;
    int8_t tiltY;
    int8_t gas;
    int8_t brake;
} UdpInputPacket;

static int udp_socket = -1;
static SceUID udp_thread = -1;
static volatile int udp_running = 0;
static UdpInputPacket remote_input = {0};

static void udp_poll(void) {
    if (udp_socket < 0)
        return;

    UdpInputPacket pkt;
    struct sockaddr_in client;
    socklen_t len = sizeof(client);

    int received = (int)sceNetInetRecvfrom(
        udp_socket,
        &pkt,
        sizeof(pkt),
        0x80, /* MSG_DONTWAIT */
        (struct sockaddr *)&client,
        &len
    );

    if (received == (int)sizeof(UdpInputPacket)) {
        remote_input.tiltX = pkt.tiltX;
        remote_input.tiltY = pkt.tiltY;
        remote_input.gas = pkt.gas;
        remote_input.brake = pkt.brake;
    }
}

static void apply_controls(void) {
    SceCtrlData *pad = (SceCtrlData *)GAME_INPUT_BASE;

    pad->Lx = (unsigned char)(remote_input.tiltX + 128);
    pad->Ly = (unsigned char)(remote_input.tiltY + 128);

    uint32_t game_buttons =
        *(volatile uint32_t *)GAME_BUTTON_CACHE;

    game_buttons &= ~(PSP_CTRL_SQUARE | PSP_CTRL_CROSS);

    if (remote_input.gas > 64)
        game_buttons |= PSP_CTRL_SQUARE;

    if (remote_input.brake < -64)
        game_buttons |= PSP_CTRL_CROSS;

    *(volatile uint32_t *)GAME_BUTTON_CACHE = game_buttons;
    pad->Buttons = game_buttons;
}

static int udp_thread_main(SceSize args, void *argp) {
    while (udp_running) {
        udp_poll();
        apply_controls();
        sceKernelDelayThread(16666);
    }

    return 0;
}

int module_start(SceSize args, void *argp) {
    struct sockaddr_in server;

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(UDP_LISTEN_PORT);
    server.sin_addr.s_addr = INADDR_ANY;

    udp_socket = sceNetInetSocket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket < 0)
        return -1;

    if (sceNetInetBind(
            udp_socket,
            (struct sockaddr *)&server,
            sizeof(server)) < 0) {
        sceNetInetClose(udp_socket);
        udp_socket = -1;
        return -1;
    }

    udp_running = 1;

    udp_thread = sceKernelCreateThread(
        "GTRemasteredUDP",
        udp_thread_main,
        0x18,
        0x1000,
        PSP_THREAD_ATTR_USER,
        NULL
    );

    if (udp_thread < 0) {
        udp_running = 0;
        sceNetInetClose(udp_socket);
        udp_socket = -1;
        return -1;
    }

    if (sceKernelStartThread(udp_thread, 0, NULL) < 0) {
        udp_running = 0;
        sceKernelDeleteThread(udp_thread);
        udp_thread = -1;
        sceNetInetClose(udp_socket);
        udp_socket = -1;
        return -1;
    }

    return 0;
}

int module_stop(SceSize args, void *argp) {
    udp_running = 0;

    if (udp_thread >= 0) {
        sceKernelWaitThreadEnd(udp_thread, NULL);
        sceKernelDeleteThread(udp_thread);
        udp_thread = -1;
    }

    if (udp_socket >= 0) {
        sceNetInetClose(udp_socket);
        udp_socket = -1;
    }

    return 0;
}

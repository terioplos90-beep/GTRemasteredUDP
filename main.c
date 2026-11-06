/*
Remastered Controls: RemasteredControls_GTpsp
Copyright (C) 2018, TheFloW
Copyright (C) 2023, Katharine Chui

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see http://www.gnu.org/licenses/.
*/

#include <pspctrl.h>
#include <pspkernel.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "hooking.h"
#include "disc_ident.h"

#define UDP_LISTEN_PORT 42069
#define PSP_CTRL_CROSS     0x00000040
#define PSP_CTRL_SQUARE    0x00000080
#define GAME_BUTTON_CACHE  0x08A12C00
#define GAME_INPUT_BASE    0x08A00000

PSP_MODULE_INFO("GTRemasteredUDP", PSP_MODULE_KERNEL, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);

int udp_socket = -1;

typedef struct {
int8_t tiltX;
int8_t tiltY;
int8_t gas;
int8_t brake;
} UdpInputPacket;

UdpInputPacket remote_input = {0};

void udp_poll() {
if (udp_socket < 0) return;
UdpInputPacket pkt;
struct sockaddr_in client;
socklen_t len = sizeof(client);

if (recvfrom(udp_socket, &pkt, sizeof(UdpInputPacket), 0,
(struct sockaddr*)&client, &len) == sizeof(UdpInputPacket)) {
memcpy(&remote_input, &pkt, sizeof(UdpInputPacket));
}
}

void apply_controls() {
SceCtrlData* pad = (SceCtrlData*)GAME_INPUT_BASE;
pad->Lx = remote_input.tiltX + 128;
pad->Ly = remote_input.tiltY + 128;

uint32_t game_buttons = (volatile uint32_t)GAME_BUTTON_CACHE;

game_buttons &= ~(PSP_CTRL_SQUARE | PSP_CTRL_CROSS);

if (remote_input.gas > 64) game_buttons |= PSP_CTRL_SQUARE;
if (remote_input.brake < -64) game_buttons |= PSP_CTRL_CROSS;

*(volatile uint32_t*)GAME_BUTTON_CACHE = game_buttons;
pad->Buttons = game_buttons;
}

int module_start(SceSize args, void *argp) {
udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
struct sockaddr_in server = {0};
server.sin_family = AF_INET;
server.sin_port = htons(UDP_LISTEN_PORT);
server.sin_addr.s_addr = INADDR_ANY;

bind(udp_socket, (struct sockaddr*)&server, sizeof(server));
fcntl(udp_socket, F_SETFL, O_NONBLOCK);

return 0;
}

int module_thread(SceSize args, void *argp) {
while(1) {
udp_poll();
apply_controls();
sceKernelDelayThread(16666);
}
return 0;
}

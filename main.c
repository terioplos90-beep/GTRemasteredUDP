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
#include <pspnet.h>
#include <pspnet_inet.h>
#include <pspkernel.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "hooking.h"
#include "disc_ident.h"
#include "logging.h"

#define GAME_MODULE_NAME "PDIAPP"
#define UDP_LISTEN_PORT 42069

#define PSP_CTRL_CROSS     0x00000040
#define PSP_CTRL_SQUARE    0x00000080
#define GAME_BUTTON_CACHE  0x08A12C00
#define GAME_INPUT_BASE    0x08A00000

PSP_MODULE_INFO("VirtualUDPad", PSP_MODULE_KERNEL, 1, 0);

int udp_socket = -1;

typedef struct {
 int8_t tiltX;
 int8_t tiltY;
 int8_t gas;
 int8_t brake;
} UdpInput;

UdpInput input = {0};

int udpSetup() {
 udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
 if (udp_socket < 0) return -1;

 struct sockaddr_in server = {0};
 server.sin_family = AF_INET;
 server.sin_port = htons(UDP_LISTEN_PORT);
 server.sin_addr.s_addr = INADDR_ANY;

 if (bind(udp_socket, (struct sockaddr*)&server, sizeof(server)) < 0) {
 close(udp_socket);
 return -1;
 }

 fcntl(udp_socket, F_SETFL, O_NONBLOCK);
 return 0;
}

void udpRead() {
 if (udp_socket < 0) return;
 UdpInput pkt;
 struct sockaddr_in client;
 socklen_t len = sizeof(client);

 if (recvfrom(udp_socket, &pkt, sizeof(pkt), 0, (struct sockaddr*)&client, &len) == sizeof(pkt)) {
 memcpy(&input, &pkt, sizeof(pkt));
 }
}

void applyControls() {
 // Terapkan stik tilt yang sudah berjalan
 SceCtrlData* pad = (SceCtrlData*)GAME_INPUT_BASE;
 pad->Lx = input.tiltX + 128;
 pad->Ly = input.tiltY + 128;

 // Terapkan tombol digital gas dan rem
 uint32_t gameBtns = *(volatile uint32_t*)GAME_BUTTON_CACHE;

 
 gameBtns &= ~(PSP_CTRL_SQUARE | PSP_CTRL_CROSS);

 if (input.gas > 64) gameBtns |= PSP_CTRL_SQUARE;
 if (input.brake < -64) gameBtns |= PSP_CTRL_CROSS;

 *(volatile uint32_t*)GAME_BUTTON_CACHE = gameBtns;
 pad->Buttons = gameBtns;
}

int moduleStart(SceSize args, void* argp) {
 udpSetup();
 return 0;
}

int moduleMain(SceSize args, void* argp) {
 while(1) {
 udpRead();
 applyControls();
 sceKernelDelayThread(16666);
 }
 return 0;

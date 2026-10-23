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
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <pspctrl.h>
#include <pspnet.h>
#include <pspnet_inet.h>
#include <pspkernel.h>
#include <netinet/in.h>

#include <string.h>

#include "hooking.h"
#include "disc_ident.h"

#include "logging.h"

#include <systemctrl.h>

#include "common.h"
#define GAME_MODULE_NAME "PDIAPP"

PSP_MODULE_INFO(MODULE_NAME, PSP_MODULE_KERNEL, 1, 0);

#define EMULATOR_DEVCTL__IS_EMULATOR     0x00000003

#ifndef IRSHELL
static STMOD_HANDLER previous;
#endif

static int is_emulator;
static u32 game_base_addr = 0;

static int override_accel = 0;
static int accel_override = 0;
static int override_brake = 0;
static int brake_override = 0;
static int override_steering = 0;
static short int steering_override = 0;
static int override_camera = 0;
static float camera_override = 0;

static unsigned char outer_deadzone = 114;
static unsigned char inner_deadzone = 10;

static int camera_controls = 0;
static int adjacent_axes = 0;

// UDP steering input for PPSSPP.
// Accepts either:
//   steering
// or:
//   angle,steering
#define UDP_STEERING_PORT 5557
#define UDP_BUF_SIZE 128
#define UDP_TIMEOUT_FRAMES 20

static volatile int udp_steering_value = 0;
static volatile int udp_packet_valid = 0;
static volatile int udp_packet_age = UDP_TIMEOUT_FRAMES + 1;
static SceUID udp_thread_id = -1;
static int udp_socket = -1;

int _atoi(char *buf);

static int parse_udp_steering(char *buf){
	char *comma = strchr(buf, ',');
	char *value_text = (comma != NULL) ? comma + 1 : buf;

	int value = _atoi(value_text);

	if(value < -67){
		value = -67;
	}
	if(value > 67){
		value = 67;
	}

	// Sender range is approximately -67..+67 degrees.
	// Plugin steering range is -127..+127.
	return value * 127 / 67;
}

static int udp_steering_thread(SceSize args, void *argp){
	struct sockaddr_in addr;
	char buf[UDP_BUF_SIZE];

	udp_socket = sceNetInetSocket(AF_INET, SOCK_DGRAM, 0);
	if(udp_socket < 0){
		LOG("UDP socket failed: %d", udp_socket);
		udp_thread_id = -1;
		sceKernelExitDeleteThread(0);
		return 0;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(UDP_STEERING_PORT);
	addr.sin_addr.s_addr = INADDR_ANY;

	int res = sceNetInetBind(udp_socket, (struct sockaddr *)&addr, sizeof(addr));
	if(res < 0){
		LOG("UDP bind failed: %d", res);
		sceNetInetClose(udp_socket);
		udp_socket = -1;
		udp_thread_id = -1;
		sceKernelExitDeleteThread(0);
		return 0;
	}

	LOG("UDP steering listening on port %d", UDP_STEERING_PORT);

	while(1){
		int fromlen = sizeof(addr);
		int len = sceNetInetRecvfrom(udp_socket, buf, UDP_BUF_SIZE - 1, 0,
			(struct sockaddr *)&addr, (socklen_t *)&fromlen);
		if(len > 0){
			if(len >= UDP_BUF_SIZE){
				len = UDP_BUF_SIZE - 1;
			}
			buf[len] = '\0';
			udp_steering_value = parse_udp_steering(buf);
			udp_packet_valid = 1;
			udp_packet_age = 0;
		}
	}

	return 0;
}

static void start_udp_steering(void){
	// This experiment targets PPSSPP only. The network libraries are initialized
	// here and are deliberately not terminated, because PPSSPP may use them too.
	int net_res = sceNetInit(128 * 1024, 0x20, 0x1000, 0x20, 0x1000);
	LOG("sceNetInit result: %d", net_res);
	int inet_res = sceNetInetInit();
	LOG("sceNetInetInit result: %d", inet_res);

	udp_thread_id = sceKernelCreateThread("GTRemasteredUDP", udp_steering_thread, 0x18, 0x4000, 0, NULL);
	if(udp_thread_id < 0){
		LOG("UDP thread create failed: %d", udp_thread_id);
		return;
	}
	int res = sceKernelStartThread(udp_thread_id, 0, NULL);
	if(res < 0){
		LOG("UDP thread start failed: %d", res);
	}
}

static int apply_deadzone(int val){
	if(val < inner_deadzone){
		return 0;
	}
	int range = outer_deadzone - inner_deadzone;
	val = val - inner_deadzone;
	if(val > range){
		val = range;
	}
	return val * 127 / range;
}

static void sample_input(SceCtrlData *pad_data, int count, int negative){
	if(count < 1){
		LOG("count is %d, processing skipped", count);
		return;
	}

	LOG_VERBOSE("processing %d buffers in %s mode", count, negative? "negative" : "positive");

	// for this game, it probably makes sense to just process the last buffer
	int rx = pad_data[count - 1].Rsrv[0];
	int ry = pad_data[count - 1].Rsrv[1];
	int lx = pad_data[count - 1].Lx;
	int ly = pad_data[count - 1].Ly;

	#if VERBOSE
	u32 timestamp = pad_data[count - 1].TimeStamp;
	#endif // VERBOSE

	// right, left, down, up

	int lxp = 0;
	int lxn = 0;
	int lyp = 0;
	int lyn = 0;
	//int rxp = 0;
	int rxn = 0;
	int ryp = 0;
	int ryn = 0;

	static int right_stick_looks_dead = 1;
	if(right_stick_looks_dead && (rx != 0 || ry != 0)){
		right_stick_looks_dead = 0;
	}

	if(lx < 128){
		lxn = apply_deadzone(128 - lx);
	}
	if(lx > 128){
		lxp = apply_deadzone(lx - 128);
	}
	if(ly < 128){
		lyn = apply_deadzone(128 - ly);
	}
	if(ly > 128){
		lyp = apply_deadzone(ly - 128);
	}
	if(rx < 128){
		rxn = apply_deadzone(128 - rx);
	}
	/*
	if(rx > 128){
		rxp = apply_deadzone(rx - 128);
	}
	*/
	if(ry < 128){
		ryn = apply_deadzone(128 - ry);
	}
	if(ry > 128){
		ryp = apply_deadzone(ry - 128);
	}

	if(adjacent_axes){
		int tmp = ryn;
		ryn = rxn;
		rxn = tmp;
	}

	override_brake = 0;
	override_accel = 0;
	override_steering = 0;
	override_camera = 0;

	if(lxp > 0){
		override_steering = 1;
		steering_override = lxp * (-1);
	}

	if(lxn > 0){
		override_steering = 1;
		steering_override = lxn;
	}

	if(!right_stick_looks_dead){
		if(ryp > 0){
			override_brake = 1;
			brake_override = ryp;
		}

		if(ryn > 0){
			override_accel = 1;
			accel_override = ryn;
		}
	}

	if(camera_controls){
		if(lyn > 0){
			override_camera = 1;
			camera_override = (float)(lyn * -1.5f) / 127.0f;
		}

		if(lyp > 0){
			override_camera = 1;
			camera_override = (float)(lyp * 1.5f) / 127.0f;
		}
	}

	LOG_VERBOSE("timestamp: %lu lx: %d ly: %d rx: %d ry: %d", timestamp, lx, ly, rx, ry);
}

static int (*sceCtrlReadBufferPositiveOrig)(SceCtrlData *pad_data, int count);
int sceCtrlReadBufferPositivePatched(SceCtrlData *pad_data, int count){
	int k1 = pspSdkSetK1(0);
	int res = sceCtrlReadBufferPositiveOrig(pad_data, count);

	sample_input(pad_data, res, 0);

	pspSdkSetK1(k1);
	return res;
}

int set_offsets(char *disc_id, char *disc_version){
	LOG("game_base_addr: 0x%lx", game_base_addr);
	// EU and US v2.00
	if(strcmp("2.00", disc_version) == 0 && (strcmp("UCES01245", disc_id) == 0 || strcmp("UCUS98632", disc_id) == 0)){
		offset_digital_to_analog = game_base_addr + 0x14eb40;
		offset_populate_car_digital_control = game_base_addr + 0x126b50;
		offset_populate_car_analog_control = game_base_addr + 0x126dec;
		return 0;
	}

	// ASIA v1.00
	if(strcmp("1.00", disc_version) == 0 && strcmp("UCAS40265", disc_id) == 0){
		offset_populate_car_analog_control = game_base_addr + 0x126dec;
		return 0;
	}

	// JP v1.01
	if(strcmp("1.01", disc_version) == 0 && strcmp("UCJS10100", disc_id) == 0){
		offset_populate_car_analog_control = game_base_addr + 0x126dd0;
		return 0;
	}

	LOG("unknown dics id %s with version %s", disc_id, disc_version);
	return -1;
}

// this maps way smoother than trying to go through the ps3 path, but then it breaks replay and ghost
static void (*digital_to_analog_orig)(u32 *param_1, u32 *param_2);
void digital_to_analog_patched(u32 *param_1, u32 *param_2){
	float *accel = (float *)((u32)(param_1[2]) + 0x530);
	u32 *accel_as_int = (u32 *)accel;
	float *brake = (float *)((u32)(param_1[2]) + 0x538);
	u32 *brake_as_int = (u32 *)brake;

	digital_to_analog_orig(param_1, param_2);
	if(*accel_as_int != 0 && override_accel){
		*accel = (float)accel_override / 127.0f;
		if(*accel > 1.0){
			*accel = 1.0;
		}
	}

	if(*brake_as_int != 0 && override_brake){
		*brake = (float)brake_override / 127.0f;
		if(*brake > 1.0){
			*brake = 1.0;
		}
	}
}

static void (*populate_car_digital_control_orig)(unsigned char *param_1, u32 param_2, u32 param_3);
void populate_car_digital_control_patched(unsigned char *param_1, u32 param_2, u32 param_3){
	unsigned short int *accel_control = (unsigned short int *)&param_1[8];
	unsigned short int *brake_control = (unsigned short int *)&param_1[10];

	populate_car_digital_control_orig(param_1, param_2, param_3);

	if(override_accel){
		*accel_control = 1;
		param_1[0] &= 0x9d;
	}
	if(override_brake){
		*brake_control = 1;
		param_1[0] &= 0xfb;
	}
}

// this is naturally invoked when button bound for steering is not pressed, so likely invoked for converting analog stick value to steering
static void (*populate_car_analog_control_orig)(u32 param_1, int *param_2, unsigned char *param_3, u32 param_4, u32 param_5, unsigned char param_6);
void populate_car_analog_control_patched(u32 param_1, int *param_2, unsigned char *param_3, u32 param_4, u32 param_5, unsigned char param_6){
	short *steering = (short *)(&param_3[4]);
	float *camera_rotation = (float *)(&param_3[0x2c]);
	short *throttle = (short *)(&param_3[0x8]);
	short *brake = (short *)(&param_3[0xA]);

	param_3[0] = 0;
	param_3[1] = 0;

	int k1 = pspSdkSetK1(0);

	static void *logged_location = NULL;
	if(param_3 != logged_location){
		logged_location = param_3;
		LOG("car control struct is now at 0x%lx", (uint32_t)param_3);
	}

	int mode;
	sceCtrlGetSamplingMode(&mode);
	if(mode != PSP_CTRL_MODE_ANALOG){
		LOG_VERBOSE("sceCtrlGetSamplingMode is not analog..? setting it to analog now");
		sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
		return;
	}

	#if 0
	if(is_emulator){
	#else
	{
	#endif
		SceCtrlData pad_data;
		int res = sceCtrlPeekBufferPositive(&pad_data, 1);
		sample_input(&pad_data, res, 0);
	}

	// UDP steering takes priority while packets are arriving.
	// If packets stop for too long, immediately return steering to center.
	if(udp_packet_valid){
		int age = udp_packet_age;
		if(age <= UDP_TIMEOUT_FRAMES){
			param_3[0] = param_3[0] | 1;
			param_3[1] = param_3[1] | 2;
			*steering = udp_steering_value * 0x2000 / 127;
			udp_packet_age = age + 1;
			LOG_VERBOSE("applying UDP steering, val is %d, steering is %d", udp_steering_value, *steering);
		}else{
			*steering = 0;
		}
	}else if(override_steering){
		param_3[0] = param_3[0] | 1;
		param_3[1] = param_3[1] | 2;
		*steering = steering_override * 0x2000 / 127;
		LOG_VERBOSE("applying steering override, val is %d, steering is %d", steering_override, *steering);
	}else{
		*steering = 0;
	}

	if(override_accel){
		param_3[0] = param_3[0] | 2;
		if(accel_override >= 95){
			static short base = (0x1000 * 0.96f);
			static short offset_throttle = (0x1000 * (1.0f - 0.96f) + 1);
			int throttle_segment = accel_override - 95;
			static int range = 127 - 95;
			*throttle = base + throttle_segment * offset_throttle / range;
			if(*throttle > 4096){
				*throttle = 4096;
			}
		}else if(accel_override >= 64){
			static short base = (0x1000 * 0.91f);
			static short offset_throttle = (0x1000 * (0.96f - 0.91f));
			int throttle_segment = accel_override - 64;
			static int range = 94 - 64;
			*throttle = base + throttle_segment * offset_throttle / range;
		}else if(accel_override >= 32){
			static short base = (0x1000 * 0.81f);
			static short offset_throttle = (0x1000 * (0.91f - 0.81f));
			int throttle_segment = accel_override - 32;
			static int range = 63 - 32;
			*throttle = base + throttle_segment * offset_throttle / range;
		}else{
			static short base = (0x1000 * 0.71f);
			static short offset_throttle = (0x1000 * (0.81f - 0.71f));
			int throttle_segment = accel_override;
			static int range = 31;
			*throttle = base + throttle_segment * offset_throttle / range;
		}
		LOG_VERBOSE("applying accel override, val is %d, throttle is %d", accel_override, *throttle);
	}

	if(override_brake){
		param_3[0] = param_3[0] | 4;
		*brake = brake_override * 0x1000 / 127;
		LOG_VERBOSE("applying brake override, val is %d, brake is %d", brake_override, *brake);
	}

	if(override_camera){
		*camera_rotation = camera_override;
	}

	pspSdkSetK1(k1);
}

int _atoi(char *buf){
	int i = 0;
	int val = 0;
	int negative = 0;

	if(buf[0] == '-'){
		negative = 1;
		i = 1;
	}

	while(1){
		if(buf[i] == '\0'){
			break;
		}
		val = val * 10;
		val = val + (int)(buf[i] - '0');
		i++;
	}

	if(negative){
		val = val * (-1);
	}

	return val;
}

void parse_config(){
	char *path = "ms0:/PSP/"MODULE_NAME"_settings.txt";
	int fd = sceIoOpen(path, PSP_O_RDONLY, 0);
	if(fd < 0){
		LOG("cannot open %s for reading, trying ef0:/", path);
		path = "ef

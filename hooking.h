#ifndef _HOOKING_
#define _HOOKING_
#include "logging.h"

#include <psputils.h>
#include <pspsdk.h>
#include <psploadcore.h>

u32 MakeSyscallStub(void *function);

#define MAKE_JUMP(a, f) _sw(0x08000000 | (((u32)(f) & 0x0FFFFFFC) >> 2), a);

#define GET_JUMP_TARGET(x) (0x80000000 | (((x) & 0x03FFFFFF) << 2))

u32 offset_digital_to_analog = 0;
u32 offset_populate_car_digital_control = 0;
u32 offset_populate_car_analog_control = 0;

#define HIJACK_FUNCTION(a, f, ptr) \
{ \
	LOG("hijacking function at 0x%lx with 0x%lx", (u32)a, (u32)f); \
	u32 _func_ = (u32)a; \
	u32 _ff = (u32)f; \
	int _interrupts = pspSdkDisableInterrupts(); \
	sceKernelDcacheWritebackInvalidateAll(); \
	if(!is_emulator){ \
		_ff = MakeSyscallStub(f); \
	} \
	static u32 patch_buffer[3]; \
	_sw(_lw(_func_), (u32)patch_buffer); \
	_sw(_lw(_func_ + 4), (u32)patch_buffer + 8);\
	MAKE_JUMP((u32)patch_buffer + 4, _func_ + 8); \
	_sw(0x08000000 | (((u32)(_ff) >> 2) & 0x03FFFFFF), _func_); \
	_sw(0, _func_ + 4); \
	ptr = (void *)patch_buffer; \
	sceKernelDcacheWritebackInvalidateAll(); \
	sceKernelIcacheClearAll(); \
	pspSdkEnableInterrupts(_interrupts); \
	LOG("original instructions: 0x%lx 0x%lx", _lw((u32)patch_buffer), _lw((u32)patch_buffer + 8)); \
}

#define HIJACK_SYSCALL_STUB(a, f, ptr) \
{ \
	LOG("hijacking syscall stub at 0x%lx with 0x%lx", (u32)a, (u32)f); \
	u32 _func_ = (u32)a; \
	u32 _ff = (u32)f; \
	int _interrupts = pspSdkDisableInterrupts(); \
	sceKernelDcacheWritebackInvalidateAll(); \
	if(!is_emulator){ \
		_func_ = GET_JUMP_TARGET(_lw(_func_)); \
	} \
	u32 _pattern[2]; \
	_sw(_lw(_func_), (u32)_pattern); \
	_sw(_lw(_func_ + 4), (u32)_pattern + 4); \
	static u32 patch_buffer[3]; \
	if(is_emulator){ \
		_sw(_lw(_func_), (u32)patch_buffer); \
		_sw(_lw(_func_ + 4), (u32)patch_buffer + 4); \
	}else{ \
		_sw(_lw(_func_), (u32)patch_buffer); \
		_sw(_lw(_func_ + 4), (u32)patch_buffer + 8); \
		MAKE_JUMP((u32)patch_buffer + 4, _func_ + 8); \
	} \
	_sw(0x08000000 | (((u32)(_ff) >> 2) & 0x03FFFFFF), _func_); \
	_sw(0, _func_ + 4); \
	ptr = (void *)patch_buffer; \
	sceKernelDcacheWritebackInvalidateAll(); \
	sceKernelIcacheClearAll(); \
	pspSdkEnableInterrupts(_interrupts); \
	if(!is_emulator){ \
		LOG("real hardware mode, retargetting function 0x%lx", _func_); \
	} \
	LOG("original instructions: 0x%lx 0x%lx", _lw((u32)_pattern), _lw((u32)_pattern + 4)); \
	if(is_emulator){ \
		static int _dumped = 0; \
		if(!_dumped){ \
			_dumped = 1; \
			SceUID _modules[32]; \
			SceKernelModuleInfo _info; \
			int _i, _count = 0; \
			if (sceKernelGetModuleIdList(_modules, sizeof(_modules), &_count) >= 0) { \
				for (_i = 0; _i < _count; _i++) { \
					_info.size = sizeof(SceKernelModuleInfo); \
					if (sceKernelQueryModuleInfo(_modules[_i], &_info) < 0) continue; \
					if (strcmp(_info.name, GAME_MODULE_NAME) != 0) continue; \
					LOG("GAME seg0 0x%x size 0x%x seg1 0x%x size 0x%x", _info.segmentaddr[0], _info.segmentsize[0], _info.segmentaddr[1], _info.segmentsize[1]); \
					u32 _seg0 = _info.segmentaddr[0]; \
					u32 _size0 = _info.segmentsize[0]; \
					u32 _start = _seg0 + _size0 - 0x8000; \
					int _found = 0; \
					u32 _j; \
					for(_j = 0; _j < 0x8000 && _found < 20; _j+=8){ \
						u32 _addr = _start + _j; \
						u32 _w0 = _lw(_addr); \
						u32 _w1 = _lw(_addr+4); \
						if((_w0 & 0xFC000000) == 0x08000000 && _w1 == 0){ \
							u32 _target = 0x80000000 | ((_w0 & 0x03FFFFFF) << 2); \
							if(_target >= 0x83200000 && _target < 0x83400000){ \
								LOG("STUB %d at 0x%x -> 0x%x", _found, _addr, _target); \
								_found++; \
							} \
						} \
					} \
					LOG("STUB total first20 done, found=%d", _found); \
					u32 _seg; \
					for(_seg = 0; _seg < 3; _seg++){ \
						u32 _a = _info.segmentaddr[_seg]; \
						u32 _s = _info.segmentsize[_seg]; \
						if(_a == 0 || _s == 0) continue; \
						for(_j = 0; _j < _s; _j+=4){ \
							u32 _addr = _a + _j; \
							u32 _v = _lw(_addr); \
							if(_v == 0x3A622550 || _v == 0x1F803E09 || _v == 0x60E4E2EB || _v == 0xC152080A){ \
								LOG("FOUND NID 0x%x at 0x%x seg %ld", _v, _addr, _seg); \
							} \
						} \
					} \
					LOG("NID scan done"); \
				} \
			} \
		} \
	} \
}

#endif

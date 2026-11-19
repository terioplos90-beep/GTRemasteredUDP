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
		SceUID _modules[32]; \
		SceKernelModuleInfo _info; \
		int _i, _count = 0; \
		if (sceKernelGetModuleIdList(_modules, sizeof(_modules), &_count) >= 0) { \
			for (_i = 0; _i < _count; _i++) { \
				_info.size = sizeof(SceKernelModuleInfo); \
				if (sceKernelQueryModuleInfo(_modules[_i], &_info) < 0) continue; \
				if (strcmp(_info.name, MODULE_NAME) == 0) continue; \
				if (strcmp(_info.name, GAME_MODULE_NAME) != 0) continue; \
				u32 _seg_addr = _info.segmentaddr[1]; \
				u32 _seg_size = _info.segmentsize[1]; \
				LOG("DUMP segment1 addr 0x%x size 0x%x", _seg_addr, _seg_size); \
				u32 _j; \
				for(_j = 0; _j < _seg_size && _j < 0x2000; _j+=8){ \
					u32 _addr = _seg_addr + _j; \
					u32 _w0 = _lw(_addr); \
					u32 _w1 = _lw(_addr+4); \
					if((_w0 & 0xFC000000) == 0x08000000){ \
						u32 _target = 0x80000000 | ((_w0 & 0x03FFFFFF) << 2); \
						LOG("STUB at 0x%x: J 0x%x NOP 0x%x", _addr, _target, _w1); \
					} \
				} \
				u32 _seg0 = _info.segmentaddr[0]; \
				u32 _size0 = _info.segmentsize[0]; \
				for(_j = 0; _j < _size0; _j+=4){ \
					u32 _addr = _seg0 + _j; \
					if(_lw(_addr) == 0x3A622550){ \
						LOG("FOUND NID sceCtrlPeekBufferPositive 0x3A622550 at 0x%x", _addr); \
						int _k; \
						for(_k = -8; _k < 32; _k++){ \
							LOG("NID dump [%d] 0x%x = 0x%x", _k, _addr + _k*4, _lw(_addr + _k*4)); \
						} \
						break; \
					} \
				} \
			} \
		} \
	} \
}

#endif

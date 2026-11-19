if(is_emulator){
    SceUID _modules[32];
    SceKernelModuleInfo _info;
    int _i, _count = 0;
    if (sceKernelGetModuleIdList(_modules, sizeof(_modules), &_count) >= 0) {
        for (_i = 0; _i < _count; _i++) {
            _info.size = sizeof(SceKernelModuleInfo);
            if (sceKernelQueryModuleInfo(_modules[_i], &_info) < 0) continue;
            if (strcmp(_info.name, MODULE_NAME) == 0) continue;
            if (strcmp(_info.name, GAME_MODULE_NAME) != 0) continue;
            // dump segment 1, cari stub JAL
            // segment 1 = 0x8cdc500, size 0x9afac
            u32 _seg_addr = _info.segmentaddr[1];
            u32 _seg_size = _info.segmentsize[1];
            LOG("DUMP segment1 addr 0x%x size 0x%x", _seg_addr, _seg_size);
            u32 _j;
            for(_j = 0; _j < _seg_size && _j < 0x2000; _j+=8){
                u32 _addr = _seg_addr + _j;
                u32 _w0 = _lw(_addr);
                u32 _w1 = _lw(_addr+4);
                // stub PPSSPP biasanya: J target ; NOP
                // J opcode = 0x08000000
                if((_w0 & 0xFC000000) == 0x08000000){
                    u32 _target = 0x80000000 | ((_w0 & 0x03FFFFFF) << 2);
                    LOG("STUB at 0x%x: J 0x%x NOP 0x%x", _addr, _target, _w1);
                }
            }
            // cari NID sceCtrl di segment 0
            // sceCtrlPeekBufferPositive NID = 0x3A622550
            // sceCtrlReadBufferPositive NID = 0x1F803E... , cari yang 0x3A622550 dulu
            u32 _seg0 = _info.segmentaddr[0];
            u32 _size0 = _info.segmentsize[0];
            for(_j = 0; _j < _size0; _j+=4){
                u32 _addr = _seg0 + _j;
                if(_lw(_addr) == 0x3A622550){
                    LOG("FOUND NID sceCtrlPeekBufferPositive 0x3A622550 at 0x%x", _addr);
                    // dump 32 word sekitarnya, biar tau urutan stub nya
                    int _k;
                    for(_k = -8; _k < 32; _k++){
                        LOG("NID dump [%d] 0x%x = 0x%x", _k, _addr + _k*4, _lw(_addr + _k*4));
                    }
                    break;
                }
            }
        }
    }
}

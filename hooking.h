if(is_emulator){
    SceUID _modules[32];
    static u32 _scan_cache[32] = {0};
    SceKernelModuleInfo _info;
    int _i, _count = 0;
    if (sceKernelGetModuleIdList(_modules, sizeof(_modules), &_count) >= 0) {
        for (_i = 0; _i < _count; _i++) {
            if(_scan_cache[_i] != 0){
                continue;
            }
            _info.size = sizeof(SceKernelModuleInfo);
            if (sceKernelQueryModuleInfo(_modules[_i], &_info) < 0) {
                continue;
            }
            if (strcmp(_info.name, MODULE_NAME) == 0) {
                continue;
            }
            LOG("scanning module %s in ppsspp mode", _info.name);
            // FIX: scan SEMUA segment, bukan cuma text_size
            // stub sceCtrl PDIAPP ada di segment 1, bukan di text
            u32 _seg;
            for(_seg = 0; _seg < _info.nsegment; _seg++){
                u32 _seg_addr = _info.segmentaddr[_seg];
                u32 _seg_size = _info.segmentsize[_seg];
                if(_seg_addr == 0 || _seg_size == 0) continue;
                LOG("scanning segment %ld addr 0x%x size 0x%x", _seg, _seg_addr, _seg_size);
                u32 _j;
                for(_j = 0; _j < _seg_size; _j+=4){
                    u32 _addr = _j + _seg_addr;
                    int _found = 0;
                    _interrupts = pspSdkDisableInterrupts();
                    if(_lw((u32)_pattern) == _lw(_addr + 0) && _lw((u32)_pattern + 4) == _lw(_addr + 4)){
                        _scan_cache[_i] = _addr;
                        _found = 1;
                    }
                    pspSdkEnableInterrupts(_interrupts);
                    if(_found){
                        break;
                    }
                }
                if(_scan_cache[_i] != 0) break;
            }
        }
        for(_i = 0; _i < _count; _i++){
            u32 _addr = _scan_cache[_i];
            if(_addr != 0){
                int _patched = 0;
                _interrupts = pspSdkDisableInterrupts();
                if(_lw((u32)_pattern) == _lw(_addr + 0) && _lw((u32)_pattern + 4) == _lw(_addr + 4)){
                    _sw(0x08000000 | (((u32)(_ff) >> 2) & 0x03FFFFFF), _addr);
                    _sw(0, _addr + 4);
                    _patched = 1;
                    sceKernelIcacheClearAll();
                }
                pspSdkEnableInterrupts(_interrupts);
                if(_patched){
                    LOG("found instruction pattern 0x%lx 0x%lx at 0x%lx, patching", _pattern[0], _pattern[1], _addr);
                }
            }
        }
    }
}

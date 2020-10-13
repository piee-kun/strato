// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <android/sharedmem.h>
#include <unistd.h>
#include <asm/unistd.h>
#include "KSharedMemory.h"
#include "KProcess.h"

namespace skyline::kernel::type {
    KSharedMemory::KSharedMemory(const DeviceState &state, size_t size, memory::MemoryState memState, KType type) : initialState(memState), KMemory(state, type) {
        fd = ASharedMemory_create("KSharedMemory", size);
        if (fd < 0)
            throw exception("An error occurred while creating shared memory: {}", fd);

        kernel.ptr = reinterpret_cast<u8*>(mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED, fd, 0));
        if (kernel.ptr == MAP_FAILED)
            throw exception("An occurred while mapping shared memory: {}", strerror(errno));

        kernel.size = size;
    }

    u8 *KSharedMemory::Map(u8 *ptr, u64 size, memory::Permission permission) {
        if (ptr && !util::PageAligned(ptr))
            throw exception("KSharedMemory was mapped to a non-page-aligned address: 0x{:X}", ptr);

        guest.ptr = reinterpret_cast<u8*>(mmap(ptr, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED | (ptr ? MAP_FIXED_NOREPLACE : 0), fd, 0));
        if (guest.ptr == MAP_FAILED)
            throw exception("An error occurred while mapping shared memory in guest");
        guest.size = size;

        state.process->memory.InsertChunk(ChunkDescriptor{
            .ptr = guest.ptr,
            .size = size,
            .permission = permission,
            .state = initialState,
        });

        return guest.ptr;
    }

    void KSharedMemory::UpdatePermission(u8* ptr, size_t size, memory::Permission permission) {
        if (ptr && !util::PageAligned(ptr))
            throw exception("KSharedMemory permission updated with a non-page-aligned address: 0x{:X}", ptr);

        if (guest.Valid()) {
            mprotect(ptr, size, permission.Get());

            if (guest.ptr == MAP_FAILED)
                throw exception("An error occurred while updating shared memory's permissions in guest");

            state.process->memory.InsertChunk(ChunkDescriptor{
                .ptr = ptr,
                .size = size,
                .permission = permission,
                .state = initialState,
            });
        }
    }

    KSharedMemory::~KSharedMemory() {
        if (kernel.Valid())
            munmap(kernel.ptr, kernel.size);

        if (guest.Valid()) {
            munmap(guest.ptr, guest.size);
            state.process->memory.InsertChunk(ChunkDescriptor{
                .ptr = guest.ptr,
                .size = guest.size,
                .state = memory::states::Unmapped,
            });
        }

        close(fd);
    }
};

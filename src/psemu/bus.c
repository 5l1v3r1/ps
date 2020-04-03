// Copyright 2020 Michael Rodriguez
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright noticeand this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
// SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
// OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
// CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include "bus.h"
#include "utility/memory.h"

// 2 MB (in bytes)
static const size_t RAM_SIZE = sizeof(uint8_t) * 2097152;

// `psemu_bus` doesn't need to know about this. The caller is supplying the
// data and thus already has access to it, and no other part of the system
// needs access to the data except through the appropriate memory load
// functions.
static uint8_t* bios_data = NULL;

// Handles processing of DMA channel 2 - GPU (lists + image data) in VRAM read
// mode.
static void dma_gpu_vram_read_process(struct psemu_bus* const bus)
{
    assert(bus != NULL);

    const uint16_t ba = bus->dma_gpu.bcr >> 16;
    const uint16_t bs = bus->dma_gpu.bcr & 0x0000FFFF;

    for (uint32_t count = 0; count != (ba * bs); ++count)
    {
        // Hack (state should be `PSEMU_GPU_TRANSFERRING_DATA`)
        psemu_gpu_gp0(&bus->gpu, 0);

        *(uint32_t *)&bus->ram[bus->dma_gpu.madr.address] = bus->gpu.gpuread;
        bus->dma_gpu.madr.address += 4;
    }
}

// Handles processing of DMA channel 2 - GPU (lists + image data) in VRAM write
// mode.
static void dma_gpu_vram_write_process(struct psemu_bus* const bus)
{
    const uint16_t ba = bus->dma_gpu.bcr >> 16;
    const uint16_t bs = bus->dma_gpu.bcr & 0x0000FFFF;

    for (uint32_t count = 0; count != (ba * bs); ++count)
    {
        const uint32_t data =
        *(uint32_t *)&bus->ram[bus->dma_gpu.madr.address];

        psemu_gpu_gp0(&bus->gpu, data);
        bus->dma_gpu.madr.address += 4;
    }
}

// Handles processing of DMA channel 2 - GPU (lists + image data) in linked
// list mode.
static void dma_gpu_list_process(struct psemu_bus* const bus)
{
    assert(bus != NULL);

    for (;;)
    {
        // Grab the header word first.
        const uint32_t header =
        *(uint32_t *)&bus->ram[bus->dma_gpu.madr.address];

        // Upper 8 bits tell us the number of words in this packet, not
        // counting the header word.
        uint32_t packet_size = (header >> 24);

        while (packet_size != 0)
        {
            bus->dma_gpu.madr.address =
            (bus->dma_gpu.madr.address + 4) & 0x001FFFFC;

            const uint32_t entry =
            *(uint32_t *)&bus->ram[bus->dma_gpu.madr.address];

            psemu_gpu_gp0(&bus->gpu, entry);
            packet_size--;
        }

        // Break the loop if the end of list marker has been reached
        //
        // XXX: In a debugging/testing setting this might be dangerous; there
        // is no way to guarantee that the end of list marker is actually
        // *there*. If it's not there, this loop will never exit.
        if (header & 0x00800000)
        {
            break;
        }
        bus->dma_gpu.madr.address = header & 0x001FFFFC;
    }
}

// Handles processing of DMA channel 6 - OTC (reverse clear OT).
static void dma_otc_process(struct psemu_bus* const bus)
{
    assert(bus != NULL);

    // I'm almost sure it is incorrect to do this.
    if (bus->dma_otc.chcr.word != 0x11000002)
    {
        bus->dma_otc.chcr.busy = false;
        return;
    }

    uint32_t count   = bus->dma_otc.bcr;
    uint32_t address = bus->dma_otc.madr.address;

    while (count--)
    {
        *(uint32_t *)&bus->ram[address] = (address - 4) & 0x00FFFFFF;
        address -= 4;
    }

    *(uint32_t *)&bus->ram[address + 4] = 0x00FFFFFF;
}

// Initializes a system bus `bus`.
void psemu_bus_init(struct psemu_bus* const bus, uint8_t* const m_bios_data)
{
    bus->ram = psemu_safe_malloc(RAM_SIZE);

    bios_data = m_bios_data;

    psemu_gpu_init(&bus->gpu);
#ifdef PSEMU_DEBUG
    bus->debug_unknown_memory_load  = NULL;
    bus->debug_unknown_memory_store = NULL;

    bus->debug_user_data = NULL;
#endif // PSEMU_DEBUG
}

// Deallocates all memory held by system bus `bus`.
void psemu_bus_fini(struct psemu_bus* const bus)
{
    assert(bus != NULL);

    psemu_gpu_fini(&bus->gpu);
    psemu_safe_free(bus->ram);
}

// Processes DMA requests and interrupts.
void psemu_bus_step(struct psemu_bus* const bus)
{
    assert(bus != NULL);

    unsigned int dpcr = bus->dpcr.word & 0x08888888;

    while (dpcr)
    {
        // Extract least significant bit.
        const unsigned int dma_enable_bit = __builtin_ctzl(dpcr);

        // Zero least significant bit.
        dpcr &= (dpcr - 1);

        switch (dma_enable_bit)
        {
            // DMA channel 2 - GPU (lists + image data)
            case 11:
                switch (bus->dma_gpu.chcr.word)
                {
                    // VramRead
                    case 0x01000200:
                        dma_gpu_vram_read_process(bus);
                        break;

                    // VramWrite
                    case 0x01000201:
                        dma_gpu_vram_write_process(bus);
                        break;

                    // List
                    case 0x01000401:
                        dma_gpu_list_process(bus);
                        break;
                }

                bus->dma_gpu.chcr.busy = false;
                break;

            // DMA channel 6 - OTC (reverse clear OT)
            case 27:
                dma_otc_process(bus);

                bus->dma_otc.chcr.busy = false;
                break;
        }
    }
}

// Clears all memory held by a system bus `bus`.
void psemu_bus_reset(struct psemu_bus* const bus)
{
    assert(bus != NULL);

    psemu_gpu_reset(&bus->gpu);

    bus->i_mask.word = 0x00000000;
    bus->i_stat.word = 0x00000000;

    bus->dpcr.word = 0x07654321;
    bus->dicr.word = 0x00000000;

    memset(bus->ram,         0x00, RAM_SIZE);
    memset(bus->scratch_pad, 0x00, 1024);

    memset(&bus->dma_otc, 0x00000000, sizeof(bus->dma_otc));
    memset(&bus->dma_gpu, 0x00000000, sizeof(bus->dma_gpu));
}

// Returns a word from system bus `bus` referenced by virtual address `vaddr`.
// Virtual -> Physical address translation takes place automatically.
uint32_t psemu_bus_load_word(const struct psemu_bus* const bus,
                             const uint32_t vaddr)
{
    assert(bus != NULL);

    // XXX: This technically isn't accurate since it clobbers the Cache Control
    // register (0xFFFE0130), but it doesn't really matter since we don't care
    // about it in the first place.
    const uint32_t paddr = vaddr & 0x1FFFFFFF;

    switch ((paddr & 0xFFFF0000) >> 16)
    {
        // [0x00000000 - 0x001FFFFF]: Main RAM (first 64K reserved for BIOS)
        case 0x0000 ... 0x001F:
            return *(uint32_t *)&bus->ram[paddr];

        case 0x1F80:
            switch ((paddr & 0x0000F000) >> 12)
            {
                // [0x1F800000 - 0x1F8003FF] - Scratchpad
                // (D-Cache used as Fast RAM)
                case 0x0:
                    return *(uint32_t *)&bus->scratch_pad[paddr & 0x00000FFF];

                // I/O Ports
                case 0x1:
                    switch (paddr & 0x00000FFF)
                    {
                        // 0x1F801070 - Interrupt status register
                        case 0x070:
                            return bus->i_stat.word;

                        // 0x1F801074 - Interrupt mask register
                        case 0x074:
                            return bus->i_mask.word;

                        // 0x1F8010A8 - DMA2 (GPU) channel control (R/W)
                        case 0x0A8:
                            return bus->dma_gpu.chcr.word;

                        // 0x1F8010E8 - DMA6 (OTC) channel control (R/W)
                        case 0x0E8:
                            return bus->dma_otc.chcr.word;

                        // 0x1F8010F0 - DMA Control Register (R/W)
                        case 0x0F0:
                            return bus->dpcr.word;

                        // 0x1F8010F4 - DMA Interrupt Register (R/W)
                        case 0x0F4:
                            return bus->dicr.word;

                        // 0x1F801810 - Receive responses to GP0(0xC0) and
                        // GP1(0x10) commands
                        case 0x810:
                            return bus->gpu.gpuread;

                        // 0x1F801814 - GPU Status Register
                        case 0x814:
                            return 0x1FF00000;

                        default:
#ifdef PSEMU_DEBUG
                            if (bus->debug_unknown_memory_load)
                            {
                                bus->debug_unknown_memory_load(bus->debug_user_data,
                                                               paddr,
                                                               PSEMU_DEBUG_WORD);
                            }
#endif // PSEMU_DEBUG
                            return 0x00000000;
                    }

                default:
#ifdef PSEMU_DEBUG
                    if (bus->debug_unknown_memory_load)
                    {
                        bus->debug_unknown_memory_load(bus->debug_user_data,
                                                       paddr,
                                                       PSEMU_DEBUG_WORD);
                    }
#endif // PSEMU_DEBUG
                    return 0x00000000;
            }

        // [0x1FC00000 - 0x1FC7FFFF]: BIOS ROM (Kernel) (4096K max)
        case 0x1FC0 ... 0x1FC7:
            return *(uint32_t *)&bios_data[paddr & 0x000FFFFF];

        default:
#ifdef PSEMU_DEBUG
            if (bus->debug_unknown_memory_load)
            {
                bus->debug_unknown_memory_load(bus->debug_user_data,
                                               paddr,
                                               PSEMU_DEBUG_WORD);
            }
#endif // PSEMU_DEBUG
            return 0x00000000;
    }
}

// Returns a halfword from system bus `bus` referenced by virtual address
// `vaddr`.
//
// Virtual -> Physical address translation takes place automatically.
uint16_t psemu_bus_load_halfword(const struct psemu_bus* const bus,
                                 const uint32_t vaddr)
{
    assert(bus != NULL);

    // XXX: This technically isn't accurate since it clobbers the Cache Control
    // register (0xFFFE0130), but it doesn't really matter since we don't care
    // about it in the first place.
    const uint32_t paddr = vaddr & 0x1FFFFFFF;

    switch ((paddr & 0xFFFF0000) >> 16)
    {
        // [0x00000000 - 0x001FFFFF]: Main RAM (first 64K reserved for BIOS)
        case 0x0000 ... 0x001F:
            return *(uint16_t *)&bus->ram[paddr];

        case 0x1F80:
            switch ((paddr & 0x0000F000) >> 12)
            {
                // [0x1F800000 - 0x1F8003FF] - Scratchpad
                // (D-Cache used as Fast RAM)
                case 0x0:
                    return *(uint16_t *)&bus->scratch_pad[paddr & 0x00000FFF];

                // I/O Ports
                case 0x1:
                    switch (paddr & 0x00000FFF)
                    {
                        // 0x1F801074 - I_MASK - Interrupt mask register
                        case 0x074:
                            return bus->i_mask.word & 0x0000FFFF;

                        default:
#ifdef PSEMU_DEBUG
                            if (bus->debug_unknown_memory_load)
                            {
                                bus->debug_unknown_memory_load(bus->debug_user_data,
                                                               paddr,
                                                               PSEMU_DEBUG_HALFWORD);
                            }
#endif // PSEMU_DEBUG
                            return 0x0000;
                    }

                default:
#ifdef PSEMU_DEBUG
                    if (bus->debug_unknown_memory_load)
                    {
                        bus->debug_unknown_memory_load(bus->debug_user_data,
                                                       paddr,
                                                       PSEMU_DEBUG_HALFWORD);
                    }
#endif // PSEMU_DEBUG
                    return 0x0000;
            }

        default:
#ifdef PSEMU_DEBUG
            if (bus->debug_unknown_memory_load)
            {
                bus->debug_unknown_memory_load(bus->debug_user_data,
                                               paddr,
                                               PSEMU_DEBUG_HALFWORD);
            }
#endif // PSEMU_DEBUG
            return 0x0000;
    }
}

// Returns a byte from system bus `bus` referenced by virtual address `vaddr`.
// Virtual -> Physical address translation takes place automatically.
uint8_t psemu_bus_load_byte(const struct psemu_bus* const bus,
                            const uint32_t vaddr)
{
    assert(bus != NULL);

    // XXX: This technically isn't accurate since it clobbers the Cache Control
    // register (0xFFFE0130), but it doesn't really matter since we don't care
    // about it in the first place.
    const uint32_t paddr = vaddr & 0x1FFFFFFF;

    switch ((paddr & 0xFFFF0000) >> 16)
    {
        // [0x00000000 - 0x001FFFFF]: Main RAM (first 64K reserved for BIOS)
        case 0x0000 ... 0x001F:
            return bus->ram[paddr];

        case 0x1F80:
            switch ((paddr & 0x000F0000) >> 12)
            {
                // [0x1F800000 - 0x1F8003FF] - Scratchpad
                // (D-Cache used as Fast RAM)
                case 0x0:
                    return bus->scratch_pad[paddr & 0x00000FFF];

                default:
#ifdef PSEMU_DEBUG
                    if (bus->debug_unknown_memory_load)
                    {
                        bus->debug_unknown_memory_load(bus->debug_user_data,
                                                       paddr,
                                                       PSEMU_DEBUG_BYTE);
                    }
#endif // PSEMU_DEBUG
                    return 0x00;
            }

        // [0x1FC00000 - 0x1FC7FFFF]: BIOS ROM (Kernel) (4096K max)
        case 0x1FC0 ... 0x1FC7:
            return bios_data[paddr & 0x000FFFFF];
        
        default:
#ifdef PSEMU_DEBUG
            if (bus->debug_unknown_memory_load)
            {
                bus->debug_unknown_memory_load(bus->debug_user_data,
                                               paddr,
                                               PSEMU_DEBUG_BYTE);
            }
#endif // PSEMU_DEBUG
            return 0x00000000;
    }
}

// Stores a word `word` into system bus `bus` referenced by virtual address
// `vaddr`.
//
// Virtual -> Physical address translation takes place automatically.
void psemu_bus_store_word(struct psemu_bus* const bus,
                          const uint32_t vaddr,
                          const uint32_t word)
{
    assert(bus != NULL);

    // XXX: This technically isn't accurate since it clobbers the Cache Control
    // register (0xFFFE0130), but it doesn't really matter since we don't care
    // about it in the first place.
    const uint32_t paddr = vaddr & 0x1FFFFFFF;

    switch ((paddr & 0xFFFF0000) >> 16)
    {
        // [0x00000000 - 0x001FFFFF]: Main RAM (first 64K reserved for BIOS)
        case 0x0000 ... 0x001F:
            *(uint32_t *)&bus->ram[paddr] = word;
            return;

        case 0x1F80:
            switch ((paddr & 0x0000F000) >> 12)
            {
                // [0x1F800000 - 0x1F8003FF] - Scratchpad
                // (D-Cache used as Fast RAM)
                case 0x0:
                    *(uint32_t *)&bus->scratch_pad[paddr & 0x00000FFF] = word;
                    return;

                // I/O Ports
                case 0x1:
                    switch (paddr & 0x00000FFF)
                    {
                        // 0x1F801070 - I_STAT - Interrupt status register
                        case 0x070:
                            // Writes are acknowledgements
                            bus->i_stat.word = word;
                            return;

                        // 0x1F801074 - I_MASK - Interrupt mask register
                        case 0x074:
                            bus->i_mask.word = word;
                            return;

                        // 0x1F8010A0 - DMA2 (GPU) base address (R/W)
                        case 0x0A0:
                            bus->dma_gpu.madr.word = word;
                            return;

                        // 0x1F8010A4 - DMA2 (GPU) block control (R/W)
                        case 0x0A4:
                            bus->dma_gpu.bcr = word;
                            return;

                        // 0x1F8010A8 - DMA2 (GPU) channel control (R/W)
                        case 0x0A8:
                            bus->dma_gpu.chcr.word = word;
                            return;

                        // 0x1F8010E0 - DMA6 (OTC) base address (R/W)
                        case 0x0E0:
                            bus->dma_otc.madr.word = word;
                            return;

                        // 0x1F8010E4 - DMA6 (OTC) block control (R/W)
                        case 0x0E4:
                            bus->dma_otc.bcr = word;
                            return;

                        // 0x1F8010E8 - DMA6 (OTC) channel control (R/W)
                        case 0x0E8:
                            bus->dma_otc.chcr.word = word;
                            return;

                        // 0x1F8010F0 - DMA Control Register (R/W)
                        case 0x0F0:
                            bus->dpcr.word = word;
                            return;

                        // 0x1F8010F4 - DMA Interrupt Register (R/W)
                        case 0x0F4:
                            bus->dicr.word = word;
                            return;

                        // 0x1F801810 - GP0 Commands
                        // (Rendering and VRAM Access)
                        case 0x810:
                            psemu_gpu_gp0(&bus->gpu, word);
                            return;

                        // 0x1F801814 - GP1 Commands (Display Control)
                        case 0x814:
                            psemu_gpu_gp1(&bus->gpu, word);
                            return;
#ifdef PSEMU_DEBUG
                        default:
                            if (bus->debug_unknown_memory_store)
                            {
                                bus->debug_unknown_memory_store(bus->debug_user_data,
                                                                paddr,
                                                                word,
                                                                PSEMU_DEBUG_WORD);
                            }
                            return;
#endif // PSEMU_DEBUG
                    }
#ifdef PSEMU_DEBUG
                default:
                    if (bus->debug_unknown_memory_store)
                    {
                        bus->debug_unknown_memory_store(bus->debug_user_data,
                                                        paddr,
                                                        word,
                                                        PSEMU_DEBUG_WORD);
                    }
                    return;
#endif // PSEMU_DEBUG
            }
#ifdef PSEMU_DEBUG
        default:
            if (bus->debug_unknown_memory_store)
            {
                bus->debug_unknown_memory_store(bus->debug_user_data,
                                                paddr,
                                                word,
                                                PSEMU_DEBUG_WORD);
            }
            return;
#endif // PSEMU_DEBUG
    }
}

// Stores a halfword `halfword` into system bus `bus` referenced by virtual
// address `vaddr`.
//
// Virtual -> Physical address translation takes place automatically.
void psemu_bus_store_halfword(struct psemu_bus* const bus,
                              const uint32_t vaddr,
                              const uint16_t halfword)
{
    assert(bus != NULL);

    // XXX: This technically isn't accurate since it clobbers the Cache Control
    // register (0xFFFE0130), but it doesn't really matter since we don't care
    // about it in the first place.
    const uint32_t paddr = vaddr & 0x1FFFFFFF;

    switch ((paddr & 0xFFFF0000) >> 16)
    {
        // [0x00000000 - 0x001FFFFF]: Main RAM (first 64K reserved for BIOS)
        case 0x0000 ... 0x001F:
            *(uint16_t *)&bus->ram[paddr] = halfword;
            return;

        case 0x1F80:
            switch ((paddr & 0x0000F000) >> 12)
            {
                // [0x1F800000 - 0x1F8003FF] - Scratchpad
                // (D-Cache used as Fast RAM)
                case 0x0:
                    *(uint16_t *)&bus->scratch_pad[paddr & 0x00000FFF] =
                    halfword;

                    return;

                // I/O Ports
                case 0x1:
                    switch (paddr & 0x00000FFF)
                    {
                        // 0x1F801074 - I_MASK - Interrupt mask register
                        case 0x074:
                            bus->i_mask.word = halfword;
                            return;
#ifdef PSEMU_DEBUG
                        default:
                            if (bus->debug_unknown_memory_store)
                            {
                                bus->debug_unknown_memory_store(bus->debug_user_data,
                                                                paddr,
                                                                halfword,
                                                                PSEMU_DEBUG_HALFWORD);
                            }
                            return;
#endif // PSEMU_DEBUG
                    }
#ifdef PSEMU_DEBUG
                default:
                    if (bus->debug_unknown_memory_store)
                    {
                        bus->debug_unknown_memory_store(bus->debug_user_data,
                                                        paddr,
                                                        halfword,
                                                        PSEMU_DEBUG_HALFWORD);
                    }
                    return;
#endif // PSEMU_DEBUG
            }
#ifdef PSEMU_DEBUG
        default:
            if (bus->debug_unknown_memory_store)
            {
                bus->debug_unknown_memory_store(bus->debug_user_data,
                                                paddr,
                                                halfword,
                                                PSEMU_DEBUG_HALFWORD);
            }
            return;
#endif // PSEMU_DEBUG
    }
}

// Stores a byte `byte` into system bus `bus` referenced by virtual address
// `vaddr`.
//
// Virtual -> Physical address translation takes place automatically.
void psemu_bus_store_byte(struct psemu_bus* const bus,
                          const uint32_t vaddr,
                          const uint8_t byte)
{
    assert(bus != NULL);

    // XXX: This technically isn't accurate since it clobbers the Cache Control
    // register (0xFFFE0130), but it doesn't really matter since we don't care
    // about it in the first place.
    const uint32_t paddr = vaddr & 0x1FFFFFFF;

    switch ((paddr & 0xFFFF0000) >> 16)
    {
        // [0x00000000 - 0x001FFFFF]: Main RAM (first 64K reserved for BIOS)
        case 0x0000 ... 0x001F:
            bus->ram[paddr] = byte;
            return;

        case 0x1F80:
            switch ((paddr & 0x0000F000) >> 12)
            {
                // [0x1F800000 - 0x1F8003FF] - Scratchpad
                // (D-Cache used as Fast RAM)
                case 0x0:
                    bus->scratch_pad[paddr & 0x00000FFF] = byte;
                    return;
#ifdef PSEMU_DEBUG
                default:
                    if (bus->debug_unknown_memory_store)
                    {
                        bus->debug_unknown_memory_store(bus->debug_user_data,
                                                        paddr,
                                                        byte,
                                                        PSEMU_DEBUG_BYTE);
                    }
                    return;
#endif // PSEMU_DEBUG
            }

#ifdef PSEMU_DEBUG
        default:
            if (bus->debug_unknown_memory_store)
            {
                bus->debug_unknown_memory_store(bus->debug_user_data,
                                                paddr,
                                                byte,
                                                PSEMU_DEBUG_BYTE);
            }
            return;
#endif // PSEMU_DEBUG
    }
}
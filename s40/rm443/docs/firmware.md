# RM-443 10.12 native runtime notes

This port targets **Nokia 6303 classic / RM-443 / MCUSW 10.12 only**.
Addresses are virtual addresses in the extracted firmware. Function pointers
with bit 0 set are Thumb; even pointers are ARM. They are not offsets in a file.
The mapped firmware image starts at `0xF0100400`.

The bootstrap uses Image.getRGB, a five-object layout, KVM method lookup and
a two-line cache synchronization stage in `native/stage.S`.

## Stage-address constraints

The stage address temporarily becomes the method's flags. It must have
`ACC_NATIVE|ACC_STATIC` set and `ACC_ABSTRACT` clear; otherwise invocation can
throw `java/lang/AbstractMethodError: cacheMarker` before native entry.

In this exact ROM, the initial `invokestatic` handler loads flags at F0DDA8C4,
tests `0x400` at F0DDA8CC, and branches to the error path at F0DDC910 before the
native test (`0x100`) at F0DDA940 and call at F0DDBF88. The bootstrap selects
an actual stage address with bit `0x400` clear, keeping the cache-range and
payload-capacity constraints. Masking flags independently of the address would
instead synchronize the wrong memory. See `findStageIndex` and the host Java
regression in `scripts/CheckStagePlacement.java`.

## Execution and ownership

1. Java creates a fullscreen Canvas and waits for Start Doom. This lets the
   native UI establish a foreground application before entering native code.
2. The existing getRGB chain forges the temporary int[] length, finds the two
   private static native declarations, and synchronizes the 44-byte ARM stage.
3. `loader.c` command 0 copies the callable loader out of the Java heap into
   `os_block_alloc_no_wait` memory, synchronizes it, and rebinds its method slot.
   This is essential: later resource reads can allocate/collect Java objects.
4. Java restores target.length **before** opening/streaming doom.bin. It streams
   4096-byte chunks using one reusable int[] command buffer and byte[] buffer.
5. Command 3 clears the patched method slot, applies only R_ARM_RELATIVE
   relocations, synchronizes the native image, and switches to a 192 KiB native
   stack. The entire engine then runs in one native invocation.
6. Native quit/error releases the LCD reservation, closes files, frees every
   engine allocation, and returns. The loader frees the image/stack, performs
   KNI_ReturnVoid, restores the caller's registers/stack, and **tail-calls** the
   OS free routine for its own allocation. It never executes freed loader RAM.
7. Java resumes only to show the returned diagnostics or launch a fresh game.

The game runs inside JAVA_TASK (task ID 16), with no Java calls during play.

`doom.bin` is a custom loader resource, not a stock dlopen library. Its header
is eight little-endian uint32 fields: magic `0x30443453`, version 1, initialized
image size, memory image size including BSS, entry offset, relocation count,
stack size, reserved zero. Initialized bytes and uint32 relocation offsets
follow. Relocations are stored after the zeroed image in native memory; the
stack occupies the final allocation region. `scripts/pack.py` rejects all
symbol relocations, unexpected addends, duplicate/misaligned/out-of-range
fixups, and unsupported ELF architectures.

## Firmware bindings

`gen_imports.py` emits interworking veneers; the generated binding list is
`build/imports.json`.

| Service | Pointer | Signature / observation |
|---|---|---|
| os_block_alloc_no_wait | `F04F0341` | `void *(unsigned bytes)`; **not** libsystem malloc |
| os_block_dealloc | `F04EFA6F` | `void(void *)` |
| os_current_time_read_in_ms | `F02D500B` | `uint32_t(void)`; counter wraps |
| os_task_own_id_read | `F04EE4FF` | no arguments |
| os_msg_receive | `F04EEFD7` | no meaningful input; blocks if queue empty |
| inferred os message count | `F04EF32D` | no input; stock task tests it before receive |
| cache sync | `F0D142A0` | ARM `void(start, byte_count)` |
| KNI_GetParameterAsObject | `F0BA6C67` | bootstrap only; parameter 1, handle address |
| KNI_ReturnVoid | `F0BA681D` | bootstrap exit only; restores operand SP |
| filesys_open | `F0579D73` | UCS-2 path, flags, mode; positive fd or error |
| filesys_read / write | `F0579E21` / `F057A019` | fd, buffer, byte count |
| filesys_lseek | `F0579E49` | fd, signed offset, origin 0/1/2; absolute position |
| filesys_close / unlink | `F0579E0B` / `F057A1EF` | fd / UCS-2 path |
| filesys_mkdir | `F057A28D` | UCS-2 path (extra mode argument unused) |
| master reserve / release | `F0D4A695` / `F0D4A705` | display=1; release also takes returned token |
| master update | `F0D4A643` | context, display=1, x, y, width, height |
| setjmp / longjmp | `F05DB4A8` / `F05DB4CC` | ARM, saves r4-r11, SP, LR; buffer >=44 bytes |

The firmware's exported `malloc`/`free` alias the browser's NW_Mem allocator
and depend on browser state; use the OS block allocator instead. The port
tracks its own OS allocations and releases them as a group after game exit.
Clang needs signed 64-bit division for Doom's FixedDiv; `div64.c`/`.S` implement
the quotient+remainder EABI. Ordinary divide, soft float, memory/string and
formatting operations use ROM exports. ASCII ctype is implemented locally so
it does not depend on a browser/firmware locale table being initialized.

## LCD layout and ownership

The physical display is 240x320. GDI format **12** / HAL format **9** uses
32-bit pixels whose high byte must be nonzero for opaque writes; the port
supplies `0xFFRRGGBB`. A zero high byte is transparent on this path, not unused
padding. Format 13 is RGB565. The master API explicitly asserts format 12, so
treating it as RGB565 would be wrong. Evidence: `gdi_get_color_index` at F0644B62,
`gdi_byte_dimensions` at F0644E8C, and F064500E (GDI-to-HAL mapping).

Routine F0DC57C8 skips zero-high-byte pixels at F0DC5978. The port uses an
opaque border (`0xFF102030`) and resampled game pixels (`RGB | 0xFF000000`).
Intermediate-alpha blending is uncharacterized.

The context begins with pixel pointer at +0, uint16 width at +4, uint16 height
at +6, and format byte at +10. Master update reads only those fields. A
four-byte HAL header immediately before pixels is `{uint16 format=9,width}`.
`gdi_display_master_buffer_update` F0D4A588 checks these fields and calls
`hw_lcd_color_window_render` F0250CC8. Master update F0D4A642 reserves/releases
the display resource and then calls `hw_lcd_color_update` F0250D32 with the
stock completion callback F0D4A476. The reservation owner at D803B260 must be -1
before acquisition. Never steal an existing reservation.

Rendering remains native at Doom's 320x200; presentation is integer resampling
to 240x180 (4:3) centered on the portrait LCD. No engine resolution hacks.

`s40_finish(0)` uses 1000 as its nonzero longjmp sentinel; native cleanup
converts it to status 0 for the launcher. `Exit 1000` is normal shutdown.

## Native message pump and keys

The task main loop is F0D1522A. Its native dispatch section at F0D15252 receives
OS messages, distinguishes timer tokens 192..488, dispatches CM messages,
processes the native CM queue, and scans subscriptions. The port mirrors that
section with a bounded receive loop. It **does not call F0D144A8**, the following
Java scheduler step. It leaves the stock saved-stack anchor D8539B40 intact.

Native APIs used:

- CM_ComMgr_ProcessPhonetMessage `F0B9DE25`
- CM_ComMgr_ProcessInternalQueue `F0B9E0EB`
- CM_ComMgr_ScanISISubscriptions `F0B9BC97`
- CM_Timer_Timeout `F04EB6F3`
- inferred internal timer handler `F0D144C1` (expects original token)
- inferred current app ID `F0D04995` (read once at startup)
- inferred event dequeue `F0D14611(app, &category, &type, &value)`

Native UI messages E104/E105/E106 enter F0CFE4BA, which extracts
big-endian key values and dispatches press/repeat/release as 1/2/3. F0BD0764
feeds native event category 1 through F0D145D4/F0D1459C into the application's
40-entry ring. F0D14610 removes one event. The low 16 bits are the signed MIDP
key value, the high bits contain application metadata. No KNI wrapper or Java
method is needed to read this ring. These are native internals shared with the
Java UI subsystem, not an independent hardware keypad driver.

Do not merely poll D81757BC (GameCanvas held keys): updates also require the
native message pump, it has fewer keys, and the accumulating event queue would
fill. Do not call getKeyStatesImpl F0CB0EC2 directly: it manipulates KVM state.
F03113A0/F0DC7DD0 are **busy-wait microdelay** thunks, not OS task sleep. Current
DG_SleepMs polls the native millisecond clock while pumping messages. Replacing
it with a verified native timer wait is a future power/performance improvement.

## Files

Use low-level filesys_* operations, not the hash-restricted POSIX/libdl open.
`s40_ng_fread` F0B3479C does not advance the cached position returned by
`s40_ng_ftell` F0B34AA8; `s40_ng_fseek` F0B34A14 does. Thus the port uses
filesys_lseek(fd,0,SEEK_CUR) for actual position. Open flags were recovered from
s40_ng_fopen F0B345FC: read=0, write/create/truncate=769, append=265, update sets
bit 1 and clears bit 0. ASCII paths are converted to UCS-2 and `/` to `\`.
No undocumented FILE structure is shared with firmware stdio.

No native rename export was established. The adapter copies, closes, then
unlinks the source; this is **not atomic** against interrupted writes. This is
called for Doom saves. Audio is disabled.

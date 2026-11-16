# RM-839 02.33 native Doom ABI

Target: Nokia 301 Dual SIM, RM-839, firmware 02.33, build date 17 May 2013.
Runtime ROM regions are `62E00000..63D05210` and `80000000..80C1B3CC`
(exclusive ends).

## Names and binding

The `fw_*` names are descriptive aliases, not firmware exports.

`abi.json` stores function pointers (odd = Thumb, even = ARM) and entry
fingerprints. `gen_imports.py` emits ARM `ldr ip; bx ip` veneers; the linker
discards unused ones. The launcher validates the fingerprints before native entry.

| Alias / role | Pointer | Contract / evidence |
|---|---|---|
| os_block_alloc_no_wait | `635790EF` | Native allocation; called by `807548F4` and the JVM's `808A3AD4` |
| os_block_dealloc | `63579A55` | Native release; JVM veneer `807565A0` |
| milliseconds | `62E8C381` | Raw-counter conversion |
| task own ID | `63589DED` | Wrapper around `6357AE56` |
| message receive | `63589E4F` | Wrapper around `635838A6`; can block on an empty queue |
| message count | `63589E57` | Wrapper around `635841CC`; use before receive |
| CM timer timeout | `635893AD` | Timer index, not encoded message token |
| CM Phonet / internal queue / subscription scan | `6358C545` / `6358C841` / `6358A467` | Native task dispatch |
| VM native timer dispatcher | `808A1479` | Original encoded token; handles indices 61..72 and 74 |
| debug timer callback | `637F25A3` | Native-only special index 73 branch copied from stock task |
| current app ID | `637AB4A9` | Reads current application pointer and its +4 field; returns -1 if absent |
| native event dequeue | `808A1691` | `(app, &category, &type, &value)` |
| filesys open / close | `635AC847` / `635AC8E3` | UCS-2 path, flags, mode / positive descriptor |
| filesys read / write | `635AC91D` / `635ACB47` | `(fd, buffer, count)` |
| filesys lseek | `635AC9D9` | `(fd, signed offset, origin)`; 32-bit wrapper over 64-bit VFS seek |
| filesys unlink / mkdir | `635ACDD9` / `635ACE87` | UCS-2 path; signed byte status |
| wrap external GDI pixels | `635E2717` | `(error*, format, width, height, flags, stride, pixels)` |
| release GDI context | `635E2C27` | Releases context/backing descriptors; external pixel storage stays ours |
| foreground native refresh | `80870C3B` | `(x, y, width, height, context, dx, dy)` |
| cache sync | `63948E50` | ARM `(start, byte_count)` |
| KNI object argument | `6376BC19` | Bootstrap only, static argument index 1 |

## ARM11 bootstrap and ownership

The native bridge at `637AE53C` calls `BLX r2` at `637AE55A` after replacing
the incoming argument registers. The RM-443 trick of supplying addresses as
method flags is therefore not used. Method access flags remain `C10A`.

The bootstrap uses `Image.getRGB`, a five-array allocation sequence,
the class+36 method table and 32-byte method records.
The method's code pointer is at +4, flags at +20, owner at +24, argument count
in the upper halfword at +28. Code lookup requires an unambiguous signature.

The 114-byte Java lookupswitch is also the initial Thumb carrier. Its ten
sorted match words contain instructions and signed **11-bit** branches.
Four skipped Java jump offsets at +40/+48/+56/+64 supply the real even RAM
start, positive byte count, cache function, and ARM loader entry. The JVM
already synchronized the carrier while loading its bytecode (`637B36E8`).
The patched literals are data loads; the carrier instructions do not change.
Verification decodes branches from the actual packaged class, including all
backward branches, rather than trusting generated constants alone.

Loader command 0 copies its own position-independent code into native OS RAM,
synchronizes it, and updates the native method slot. Java then restores every
carrier literal and the forged array length before any resource allocation.
The loader survives Java GC while `doom.bin` is transferred in 4096-byte chunks.

Command 3 clears the method slot, applies only `R_ARM_RELATIVE` fixups, syncs
code, and switches to a private 192 KiB stack for the entire game. No Java API
is called during gameplay. On return it frees the game image/stack, restores
the KVM operand stack, and tail-calls OS free for its own loader allocation.
No instruction runs in freed loader RAM. Command 4 performs aborted-load cleanup.

RM-839 has explicit wrapper cleanup, copied from `808A7020..808A7026`:
read `CurrentThread` at `6015A468`, load its saved operand SP at +0x24, and
store that to `6015A420`. Do not reuse RM-443's KNI_ReturnVoid address.
The loader never allocates KVM objects or manipulates temporary Java roots.

Image format and descriptor match the 6303 loader: S4D0 header version 1,
initialized bytes, zero-filled BSS, relative offsets, then a 192 KiB stack.
Descriptor words 0..14 are status/bounds, 16..79 are error text, 80 the method
slot, 82..84 loader addresses/size, and 96..1119 the chunk buffer. Diagnostic
text is allocated in Java only after native state is restored.

## Native pump and input

The stock task loop is at `808A2426..808A24AE`; native dispatch starts
at `808A2452`. Tokens **192..441** are timers (subtract 192), not RM-443's
192..488. Other pointers whose first byte is not 1 go to the CM message
processor. Process the internal queue and scan subscriptions afterward.

Do not call `808A1460`: it is the subsequent Java scheduling step. Do not
replace the stock saved-stack anchor at `60C3F804` with the game's stack.
The Doom pump bounds work per call and checks queue count before receive.
`DG_SleepMs` polls native milliseconds while dispatching native messages.

`808A1690` consumes a **100-entry, 16-byte** event ring, unlike RM-443's
40-entry, 12-byte ring. Its three output fields remain category, type, value.
It resolves the app via `80781834 -> 807811B0`, then advances the read index
modulo 100. Keys use category 1, press/repeat/release types 1/2/3; repeats are
discarded. Signed key values come from the low 16 bits. Native focus-loss
category 2/type 4 ends the session.

## Display path

The 301 backend uses a different path from RM-443's master-display reservation.
It supplies a native **managed GDI context** around an external 240x320 buffer,
format 12, flags `0x85`, stride 960. These are the arguments used by the
native Pixmap RGB wrapper at `808A6F6A -> 805979C0 -> 635E2716`.
The wrapper preserves external backing storage; its context must be released
before the port frees the pixel buffer. A forged short context is insufficient
for this target's GDI locking/backpointer checks.

`80870C3A` is the native refresh beneath Graphics.refresh and application
dispatch (`808A5234 -> 807823EA -> 807812A2`). The port calls that native
refresh directly, bypassing the KNI/Java wrappers. It acquires native UI lock
50, checks native foreground surface/state at `60352DF8`, clips the rectangle,
and calls `80870A7A`. That routine performs the synchronous GDI copy through
`6377263E`, marks the dirty rectangle, and wakes task 27 through `808721EC`.
The firmware's native painter task performs the subsequent display update.
The source is already copied before the call returns, so the next frame can
reuse the source buffer. No Java repaint or per-frame bridge is involved.

Every pixel is opaque `0xFFRRGGBB`.
Doom's 320x200 image is resampled to 240x180 at y=70, preserving 4:3 display
aspect. Fullscreen Canvas establishes the foreground native surface before
launch.

## Files and C runtime

`635AC846` validates/translates the UCS-2 path and passes flags|0x800 to VFS
callback +52. Close/read/seek/write use callback offsets +56/+60/+64/+68.
The seek wrapper at `635AC9D8` sign-extends the 32-bit offset into r2/r3,
puts origin on the stack, and returns the low word of the 64-bit result.
The adapter's flags remain read=0, write/create/truncate=769, append=265;
update sets bit 1 and clears bit 0. Paths use DOS drive letters and backslashes.

The C/math/soft-float routines are linked from Arm GNU newlib/libm/libgcc into
the native image. They are ordinary native code, not firmware POSIX exports
or Java APIs. Generic ARMv4T soft-float archive variants work on ARM11 and
avoid ARMv7 MOVW/MOVT absolute relocation pairs. All accepted fixups are image
relative, including library data and literal pools; no host syscalls remain.

The port's private FILE API is prefixed `s40_` in its header to avoid collisions
with newlib's internal string-formatting FILE structures. `_malloc_r` and its
companions use the same tracked OS allocator as Doom. There is no sbrk heap.
The C-only build declines EHABI exception unwinding; errors/quit use setjmp
and longjmp (the linked implementation saves 40 bytes into our 64-byte buffer).
Audio remains disabled; configuration persistence and save-rename limitations
are recorded in README.md.

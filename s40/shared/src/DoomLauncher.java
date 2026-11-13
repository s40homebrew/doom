/* SPDX-License-Identifier: MIT */
/** Doom-specific presentation; resolver and bootstrap belong to the runtime. */
public final class DoomLauncher extends HomebrewMIDlet {
    protected String applicationName() {
        return "Doom Adaptive";
    }
    protected String startLabel() {
        return "Start Doom";
    }

    protected String[] instructions(int inputLayout) {
        if (inputLayout == FirmwareApi.INPUT_QWERTY)
            return new String[] {"DOOM / C3 QWERTY or QWERTZ", "IWAD: D:/doom/doom1.wad",
                    "D-pad: move / turn", "Space / center: fire", "Enter: use / select",
                    "I / O: strafe; K / Shift: run", "L: map; P: next weapon",
                    "Backspace / R softkey: menu", "Menu: center selects, Backspace backs"};
        return new String[] {"ADAPTIVE DOOM / S40", "Put doom1.wad in D:/doom/",
                "or C:/predefgallery/doom/", "D-pad / 2 4 6 8: move", "5: fire / select; 0: use",
                "1 / 3: strafe; 7: run", "9: map; #: next weapon", "*: menu; L softkey: select",
                "Check firmware first, or Start Doom"};
    }
}

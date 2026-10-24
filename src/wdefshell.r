/*
	Copyright 2017 Wolfgang Thaller.

	This file is part of Retro68.

	Retro68 is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	Retro68 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Retro68.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "Processes.r"
#include "Menus.r"
#include "Windows.r"
#include "MacTypes.r"
#include "Dialogs.r"
#include "Finder.r"

resource 'MENU' (128) {
    128, textMenuProc;
    allEnabled, enabled;
    apple;
    {
       "Notion Debug", noIcon, noKey, noMark, plain;   /* item 1 */
       "-", noIcon, noKey, noMark, plain;
    }
};

resource 'MENU' (129) {
    129, textMenuProc;
    allEnabled, enabled;
    "Notion";
    {
        "About", noIcon, noKey, noMark, plain;                /* item 1 */
        "-", noIcon, noKey, noMark, plain;
        "Settings", noIcon, noKey, noMark, plain;     /* item 3 */
        "Library", noIcon, noKey, noMark, plain;      /* item 4 */
        "-", noIcon, noKey, noMark, plain;
        "Full width", noIcon, "F", noMark, plain;     /* item 6 */
        "Quit", noIcon, "Q", noMark, plain;           /* item 7 */
    }
};

resource 'MENU' (130) {
    130, textMenuProc;
    allEnabled, enabled;
    "Text";
    {
        "Copy", noIcon, "C", noMark, plain;            /* item 1 */
        "Paste", noIcon, "V", noMark, plain;           /* item 2 */
        "-", noIcon, noKey, noMark, plain;
        "Bold", noIcon, "B", noMark, bold;             /* item 4 */
        "Italic", noIcon, "I", noMark, italic;         /* item 5 */
        "Underline", noIcon, "U", noMark, underline;   /* item 6 */
        "Strikethrough", noIcon, noKey, noMark, plain; /* item 7 */
    }
};

resource 'MENU' (133) {
    133, textMenuProc;
    allEnabled, enabled;
    "Format";
    {
        "Delete", noIcon, noKey, noMark, plain;        /* item 1 */
        "-", noIcon, noKey, noMark, plain;
        "Move   \0x11UP", noIcon, noKey, noMark, plain;   /* item 3 */
        "Move   \0x11DOWN", noIcon, noKey, noMark, plain; /* item 4 */
        "-", noIcon, noKey, noMark, plain;
        "Text", noIcon, noKey, noMark, plain;          /* item 6 */
        "Heading 1", noIcon, noKey, noMark, plain;
        "Heading 2", noIcon, noKey, noMark, plain;
        "Heading 3", noIcon, noKey, noMark, plain;
        "Bulleted list", noIcon, noKey, noMark, plain;
        "Numbered list", noIcon, noKey, noMark, plain;
        "To-do list", noIcon, noKey, noMark, plain;
        "Toggle list", noIcon, noKey, noMark, plain;
        "Code", noIcon, noKey, noMark, plain;
        "Quote", noIcon, noKey, noMark, plain;
        "Callout", noIcon, noKey, noMark, plain;
        "Toggle Heading 1", noIcon, noKey, noMark, plain;
        "Toggle Heading 2", noIcon, noKey, noMark, plain;
        "Toggle Heading 3", noIcon, noKey, noMark, plain;
    }
};

resource 'MENU' (131) {
    131, textMenuProc;
    allEnabled, enabled;
    "Insert";
    {
        "Text", noIcon, "T", noMark, plain;          /* item  1 → doAddBlock(0)  paragraph          */
        "Heading 1", noIcon, "1", noMark, plain;     /* item  2 → doAddBlock(1)  heading_1           */
        "Heading 2", noIcon, "2", noMark, plain;     /* item  3 → doAddBlock(2)  heading_2           */
        "Heading 3", noIcon, "3", noMark, plain;     /* item  4 → doAddBlock(3)  heading_3           */
        "Bulleted list", noIcon, "-", noMark, plain; /* item  5 → doAddBlock(4)  bulleted_list_item  */
        "Numbered list", noIcon, "N", noMark, plain; /* item  6 → doAddBlock(5)  numbered_list_item  */
        "To-do list", noIcon, "[", noMark, plain;    /* item  7 → doAddBlock(6)  to_do               */
        "Toggle list", noIcon, "G", noMark, plain;   /* item  8 → doAddBlock(7)  toggle              */
        "Code", noIcon, noKey, noMark, plain;        /* item  9 → doAddBlock(8)  code                */
        "Quote", noIcon, noKey, noMark, plain;       /* item 10 → doAddBlock(9)  quote               */
        "Callout", noIcon, noKey, noMark, plain;     /* item 11 → doAddBlock(10) callout             */
        "Toggle Heading 1", noIcon, noKey, noMark, plain; /* item 12 → doAddBlock(11) toggle_heading_1 */
        "Toggle Heading 2", noIcon, noKey, noMark, plain; /* item 13 → doAddBlock(12) toggle_heading_2 */
        "Toggle Heading 3", noIcon, noKey, noMark, plain; /* item 14 → doAddBlock(13) toggle_heading_3 */
        "Divider", noIcon, "=", noMark, plain;       /* item 15 → doAddBlock(14) divider             */
        "-", noIcon, noKey, noMark, plain;
        "Emoji", noIcon, "E", noMark, plain;         /* item 17 → ShowEmojiPicker  */
        "Link to page", noIcon, "L", noMark, plain;   /* item 18 → doInsertPageLink */
    }
};

resource 'MENU' (132) {
    132, textMenuProc;
    allEnabled, enabled;
    "Page";
    {
        "Reload", noIcon, "R", noMark, plain;          /* item  1 */
        "Save", noIcon, "S", noMark, plain;            /* item  2 */
        "Set icon", noIcon, noKey, noMark, plain;      /* item  3 */
        "-", noIcon, noKey, noMark, plain;
        "Open", noIcon, "O", noMark, plain;            /* item  5 */
        "New page", noIcon, "N", noMark, plain;        /* item  6 */
        "-", noIcon, noKey, noMark, plain;
        "Back", noIcon, noKey, noMark, plain;                   /* item  8 — Cmd+Left  */
        "Forward", noIcon, noKey, noMark, plain;                /* item  9 — Cmd+Right */
        "-", noIcon, noKey, noMark, plain;
        /* items 11+: recent pages, appended dynamically at runtime */
    }
};

resource 'MBAR' (128) {
    { 128, 129, 132, 130, 133, 131 };
};



data 'TEXT' (128) {
    "Version 1.0\r\r"
    "For Macintosh System 7 - 9\r"
    "Made with Retro68 * Claude code\r\r"
    "2026  by Lin van der Slikke"
};

resource 'WIND' (128) {
    {0, 0, 240, 320}, altDBoxProc;
    invisible;
    goAway;
    0, "Notion classic";
    centerMainScreen;
};

/* The 10-byte code resource stub trick.
 *
 * The bytes in this resource are 68K machine code for
 *     move.l L1(pc), -(sp)    | 2F3A 0004
 *     rts                     | 4E75
 * L1: dc.l 0x00000000         | 0000 0000
 *
 * The application loads this resource and replaces the final four bytes
 * with the address of the WDEF function in wdef.c, which is compiled as part
 * of the application.
 */
//data 'WDEF' (128) {
 //   $"2F3A 0004 4E75 0000 0000"
//};

/* ------------------------------------------------------------------ */
/* Pointing hand cursor — used when hovering over clickable links      */
/* CURS layout: 32 bytes image | 32 bytes mask | 2+2 bytes hotspot    */
/* ------------------------------------------------------------------ */
data 'CURS' (128) {
    /* image — exact pixel data from reference hand.png */
    $"3000 4800 4B00 24E0 2498 1014 1004 3004"
    $"4004 4004 4008 2008 1010 0810 0810 0000"
    /* mask — filled bounding box per row */
    $"3000 7800 7F00 3FE0 3FF8 1FFC 1FFC 3FFC"
    $"7FFC 7FFC 7FF8 3FF8 1FF0 0FF0 0FF0 0000"
    /* hotspot: row=0, col=2 (fingertip) */
    $"0000 0002"
};

/* ------------------------------------------------------------------ */
/* Settings dialog                                                     */
/* ------------------------------------------------------------------ */

/* Panel 1 — Integration Token entry */
resource 'DLOG' (129) {
    {0, 0, 148, 440},
    movableDBoxProc,
    invisible,
    noGoAway,
    0x0,
    129,
    "Connection",
    centerMainScreen
};

resource 'DITL' (129) {
    {
        /* 1: Continue button */
        {116, 344, 136, 428},
        Button { enabled, "Continue" };

        /* 2: Cancel button */
        {116, 254, 136, 332},
        Button { enabled, "Cancel" };

        /* 3: "Integration Token" label — drawn by custom proc */
        {14, 10, 30, 428},
        UserItem { disabled };

        /* 4: Token edit field */
        {54, 10, 70, 428},
        EditText { enabled, "" };

        /* 5: Token hint — drawn by custom proc */
        {30, 10, 46, 428},
        UserItem { disabled };
    }
};

/* Panel 2 — Page selection (list style) */
resource 'DLOG' (130) {
    {0, 0, 240, 320},
    movableDBoxProc,
    invisible,
    noGoAway,
    0x0,
    130,
    "Open page",
    centerMainScreen
};

resource 'DITL' (130) {
    {
        /* 1: Open button */
        {208, 228, 228, 310},
        Button { enabled, "Open" };

        /* 2: Back / Cancel button */
        {208, 138, 228, 220},
        Button { enabled, "Back" };

        /* 3: Title label — drawn by custom proc */
        {10, 10, 28, 310},
        UserItem { disabled };

        /* 4: Page list area — managed by List Manager (right=305, scrollbar=305..320) */
        {34, 0, 196, 305},
        UserItem { enabled };
    }
};

/* Library window — tree page picker used from all three entry points */
resource 'DLOG' (131) {
    {0, 0, 294, 400},
    noGrowDocProc,
    invisible,
    goAway,
    0x0,
    131,
    "Library",
    centerMainScreen
};

resource 'DITL' (131) {
    {
        /* 1: Primary action button ("Open" / "Insert link") — thick border */
        {258, 284, 278, 386},
        Button { enabled, "Open" };

        /* 2: Cancel button */
        {258, 180, 278, 270},
        Button { enabled, "Cancel" };

        /* 3: "+ New page" plain-text item — left side of footer */
        {259, 8, 279, 116},
        UserItem { enabled };

        /* 4: Scrollable content area — full height; search bar is drawn as first virtual row */
        {0, 0, 242, 384},
        UserItem { enabled };

        /* 5: Placeholder — always hidden; kept so item numbers stay stable */
        {285, 397, 289, 399},
        Button { enabled, "" };

        /* 6: Search / list divider line (1-black-2-white pattern, 7px higher) */
        {32, 0, 33, 400},
        UserItem { disabled };

        /* 7: List / footer divider line */
        {242, 0, 243, 400},
        UserItem { disabled };
    }
};

/* Unsaved-changes confirmation — shown before reload, history nav, or quit */
resource 'DLOG' (132) {
    {0, 0, 100, 300},
    noGrowDocProc,
    invisible,
    goAway,
    0x0,
    132,
    "Unsaved Changes",
    centerMainScreen
};

resource 'DITL' (132) {
    {
        /* 1: Save button (default — outlined by code) */
        {68, 210, 88, 290},
        Button { enabled, "Save" };

        /* 2: Don't Save button */
        {68, 10, 88, 116},
        Button { enabled, "Don't Save" };

        /* 3: Message */
        {12, 10, 58, 290},
        StaticText { disabled, "You have unsaved changes. Do you want to save them?" };
    }
};

/* Library: a page can't be moved to the top level through the API */
resource 'ALRT' (133) {
    {0, 0, 116, 330},
    133,
    {   OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent
    },
    alertPositionMainScreen
};

resource 'DITL' (133) {
    {
        /* 1: OK */
        {84, 250, 104, 318},
        Button { enabled, "OK" };

        /* 2: Message (Note icon sits left of it) */
        {10, 64, 78, 320},
        StaticText { disabled, "Sorry, Notion classic can't move the page to the "
                               "top level. Make the change in Notion, then Reload "
                               "the classic Library." };
    }
};

/* A link, Back or Forward led to a page Notion can't find (404) */
resource 'ALRT' (135) {
    {0, 0, 116, 330},
    135,
    {   OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent
    },
    alertPositionMainScreen
};

resource 'DITL' (135) {
    {
        /* 1: OK */
        {84, 250, 104, 318},
        Button { enabled, "OK" };

        /* 2: Message (Note icon sits left of it) */
        {10, 64, 78, 320},
        StaticText { disabled, "Sorry, Notion can't find this page. It may "
                               "have been deleted, or it isn't shared with "
                               "your integration." };
    }
};

/* Library: deleting a page also deletes its subpages (^0 = message) */
resource 'ALRT' (134) {
    {0, 0, 116, 330},
    134,
    {   OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent
    },
    alertPositionMainScreen
};

resource 'DITL' (134) {
    {
        /* 1: Cancel (default: Return doesn't delete) */
        {84, 250, 104, 318},
        Button { enabled, "Cancel" };

        /* 2: Delete */
        {84, 162, 104, 238},
        Button { enabled, "Delete" };

        /* 3: Message (Caution icon sits left of it) */
        {10, 64, 78, 320},
        StaticText { disabled, "^0" };
    }
};

resource 'SIZE' (-1) {
    reserved,
    acceptSuspendResumeEvents,
    reserved,
    canBackground,
    doesActivateOnFGSwitch,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreChildDiedEvents,
    is32BitCompatible,
    isHighLevelEventAware,
    onlyLocalHLEvents,
    notStationeryAware,
    dontUseTextEditServices,
    notDisplayManagerAware,
    reserved,
    reserved,
    4 * 1024 * 1024,    /* preferred: 4 MB */
    2 * 1024 * 1024     /* minimum:   2 MB */
};

/* ------------------------------------------------------------------ */
/* Application icon                                                    */
/* ------------------------------------------------------------------ */

data 'NCls' (0) {
};

resource 'FREF' (128) {
    'APPL', 0, ""
};

resource 'BNDL' (128) {
    'NCls', 0,
    {
        'FREF', { 0, 128 };
        'ICN#', { 0, 128 }
    }
};


data 'ics#' (-16455) {
    /* 1-bit small icon 16×16 (16 words icon + 16 words mask) */
    $"7FF0 4008 6004 7FFE"
    $"7002 7002 7312 7392"
    $"7292 72D2 7272 7232"
    $"7002 3002 1FFC 0000"
    /* mask */
    $"7FF0 7FF8 7FFC 7FFE"
    $"7FFE 7FFE 7FFE 7FFE"
    $"7FFE 7FFE 7FFE 7FFE"
    $"7FFE 3FFE 1FFC 0000"
};

data 'ICN#' (-16455) {
    /* 1-bit icon (32 rows × 4 bytes) */
    $"00000000 1FFFFFE0 3FFFFFF0 3C000038"
    $"3E00001C 3FFFFFFC 3FFFFFFC 3E00000C"
    $"3E00000C 3E00000C 3E3E07CC 3E3E07CC"
    $"3E1F038C 3E1F838C 3E1FC38C 3E1FE38C"
    $"3E1FF38C 3E1DFB8C 3E1CFF8C 3E1C7F8C"
    $"3E1C3F8C 3E1C1F8C 3E1C0F8C 3E3E0F8C"
    $"3E3E078C 3E00000C 3E00000C 3F00001C"
    $"1FFFFFFC 0FFFFFF8 00000000 00000000"
    /* mask (32 rows × 4 bytes) */
    $"00000000 1FFFFFE0 3FFFFFF0 3FFFFFF8"
    $"3FFFFFFC 3FFFFFFC 3FFFFFFC 3FFFFFFC"
    $"3FFFFFFC 3FFFFFFC 3FFFFFFC 3FFFFFFC"
    $"3FFFFFFC 3FFFFFFC 3FFFFFFC 3FFFFFFC"
    $"3FFFFFFC 3FFFFFFC 3FFFFFFC 3FFFFFFC"
    $"3FFFFFFC 3FFFFFFC 3FFFFFFC 3FFFFFFC"
    $"3FFFFFFC 3FFFFFFC 3FFFFFFC 3FFFFFFC"
    $"1FFFFFFC 0FFFFFF8 00000000 00000000"
};

data 'ics#' (128) {
    /* 1-bit small icon 16×16 (16 words icon + 16 words mask) */
    $"7FF0 4008 6004 7FFE"
    $"7002 7002 7312 7392"
    $"7292 72D2 7272 7232"
    $"7002 3002 1FFC 0000"
    /* mask */
    $"7FF0 7FF8 7FFC 7FFE"
    $"7FFE 7FFE 7FFE 7FFE"
    $"7FFE 7FFE 7FFE 7FFE"
    $"7FFE 3FFE 1FFC 0000"
};

data 'ICN#' (128) {
    /* 1-bit icon (32 rows × 4 bytes) */
    $"00000000 1FFFFFE0 3FFFFFF0 3C000038"
    $"3E00001C 3FFFFFFC 3FFFFFFC 3E00000C"
    $"3E00000C 3E00000C 3E3E07CC 3E3E07CC"
    $"3E1F038C 3E1F838C 3E1FC38C 3E1FE38C"
    $"3E1FF38C 3E1DFB8C 3E1CFF8C 3E1C7F8C"
    $"3E1C3F8C 3E1C1F8C 3E1C0F8C 3E3E0F8C"
    $"3E3E078C 3E00000C 3E00000C 3F00001C"
    $"1FFFFFFC 0FFFFFF8 00000000 00000000"
    /* mask (32 rows × 4 bytes) */
    $"00000000 1FFFFFE0 3FFFFFF0 3FFFFFF8"
    $"3FFFFFFC 3FFFFFFC 3FFFFFFC 3FFFFFFC"
    $"3FFFFFFC 3FFFFFFC 3FFFFFFC 3FFFFFFC"
    $"3FFFFFFC 3FFFFFFC 3FFFFFFC 3FFFFFFC"
    $"3FFFFFFC 3FFFFFFC 3FFFFFFC 3FFFFFFC"
    $"3FFFFFFC 3FFFFFFC 3FFFFFFC 3FFFFFFC"
    $"3FFFFFFC 3FFFFFFC 3FFFFFFC 3FFFFFFC"
    $"1FFFFFFC 0FFFFFF8 00000000 00000000"
};

/* Page icon — 16×16 1-bit outline document with folded top-right corner. */
/* Used by BT_CHILD_PAGE rows in block_render.cpp.                        */
data 'SICN' (200, "PageIcon") {
    $"0000 0000 1FC0 1020 1020 1038 1008 1008"
    $"1008 1008 1008 1008 1008 1FF8 0000 0000"
};

/* Checked checkbox — 16×16 box with X diagonals (DECOR_CHECKBOX checked state). */
data 'SICN' (201, "CheckedBox") {
    $"0000 0000 3FFC 300C 2814 2424 2244 2184"
    $"2184 2244 2424 2814 300C 3FFC 0000 0000"
};

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
       "About Notion classic", noIcon, noKey, noMark, plain;
       "-", noIcon, noKey, noMark, plain;
    }
};

resource 'MENU' (129) {
    129, textMenuProc;
    allEnabled, enabled;
    "Notion";
    {
        "Reload page", noIcon, "R", noMark, plain;
        "Save", noIcon, "S", noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Connection", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Full Width", noIcon, "F", noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Quit", noIcon, "Q", noMark, plain;
    }
};

resource 'MENU' (130) {
    130, textMenuProc;
    allEnabled, enabled;
    "Edit";
    {
        "Delete", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Copy", noIcon, "C", noMark, plain;
        "Paste", noIcon, "V", noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Bold", noIcon, "B", noMark, bold;
        "Italic", noIcon, "I", noMark, italic;
        "Underline", noIcon, "U", noMark, underline;
        "Strikethrough", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Move   \0x11UP", noIcon, noKey, noMark, plain;
        "Move   \0x11DOWN", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Text", noIcon, noKey, noMark, plain;
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
    "Add";
    {
        "Text", noIcon, "T", noMark, plain;          /* item  1 → doAddBlock(0)  paragraph          */
        "Heading 1", noIcon, "1", noMark, plain;     /* item  2 → doAddBlock(1)  heading_1           */
        "Heading 2", noIcon, "2", noMark, plain;     /* item  3 → doAddBlock(2)  heading_2           */
        "Heading 3", noIcon, "3", noMark, plain;     /* item  4 → doAddBlock(3)  heading_3           */
        "Bulleted list", noIcon, "L", noMark, plain; /* item  5 → doAddBlock(4)  bulleted_list_item  */
        "Numbered list", noIcon, "N", noMark, plain; /* item  6 → doAddBlock(5)  numbered_list_item  */
        "To-do list", noIcon, "D", noMark, plain;    /* item  7 → doAddBlock(6)  to_do               */
        "Toggle", noIcon, "G", noMark, plain;        /* item  8 → doAddBlock(7)  toggle              */
        "Code", noIcon, noKey, noMark, plain;        /* item  9 → doAddBlock(8)  code                */
        "Quote", noIcon, noKey, noMark, plain;       /* item 10 → doAddBlock(9)  quote               */
        "Callout", noIcon, noKey, noMark, plain;     /* item 11 → doAddBlock(10) callout             */
        "Toggle Heading 1", noIcon, noKey, noMark, plain; /* item 12 → doAddBlock(11) toggle_heading_1 */
        "Toggle Heading 2", noIcon, noKey, noMark, plain; /* item 13 → doAddBlock(12) toggle_heading_2 */
        "Toggle Heading 3", noIcon, noKey, noMark, plain; /* item 14 → doAddBlock(13) toggle_heading_3 */
        "Divider", noIcon, "-", noMark, plain;       /* item 15 → doAddBlock(14) divider             */
        "-", noIcon, noKey, noMark, plain;
        "Emoji", noIcon, "E", noMark, plain;         /* item 17 → ShowEmojiPicker */
        "Page Icon", noIcon, noKey, noMark, plain;   /* item 18 → ShowIconPicker  */
    }
};

resource 'MBAR' (128) {
    { 128, 129, 130, 131 };
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
/* Settings dialog                                                     */
/* ------------------------------------------------------------------ */

resource 'DLOG' (129) {
    {0, 0, 218, 440},
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
        /* 1: Save button (default) */
        {186, 350, 206, 428},
        Button { enabled, "Save" };

        /* 2: Cancel button */
        {186, 254, 206, 332},
        Button { enabled, "Cancel" };

        /* 3: Notion Page label — drawn by custom proc */
        {14, 10, 30, 428},
        UserItem { disabled };

        /* 4: Page ID edit field */
        {54, 10, 70, 428},
        EditText { enabled, "" };

        /* 5: Integration Token label — drawn by custom proc */
        {104, 10, 120, 428},
        UserItem { disabled };

        /* 6: Integration Token edit field */
        {144, 10, 160, 428},
        EditText { enabled, "" };

        /* 7: Page ID hint — drawn by custom proc */
        {30, 10, 46, 428},
        UserItem { disabled };

        /* 8: Integration Token hint — drawn by custom proc */
        {120, 10, 136, 428},
        UserItem { disabled };
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
    1000 * 2048,
    1000 * 2048
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

data 'ICN#' (-16455) {
    /* 1-bit icon (32 rows × 4 bytes) */
    $"7FFFFFE0 FFFFFFF0 F0000018 F800000C"
    $"FFFFFFFE FFFFFFFF FE000007 FC000007"
    $"FC000007 FC3E07E7 FC3F07E7 FC3F8187"
    $"FC1FC187 FC1FE187 FC1FF187 FC1FF987"
    $"FC1FFD87 FC1DFF87 FC1CFF87 FC1C7F87"
    $"FC1C3F87 FC1C1F87 FC1C0F87 FC1C0787"
    $"FC3E0387 FC3E0187 FC000007 FC000007"
    $"7E000007 3FFFFFFF 1FFFFFFF 0FFFFFFE"
    /* mask (32 rows × 4 bytes) */
    $"7FFFFFE0 FFFFFFF0 FFFFFFF8 FFFFFFFC"
    $"FFFFFFFE FFFFFFFF FFFFFFFF FFFFFFFF"
    $"FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF"
    $"FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF"
    $"FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF"
    $"FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF"
    $"FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF"
    $"7FFFFFFF 3FFFFFFF 1FFFFFFF 0FFFFFFE"
};

data 'ICN#' (128) {
    /* 1-bit icon (32 rows × 4 bytes) */
    $"7FFFFFE0 FFFFFFF0 F0000018 F800000C"
    $"FFFFFFFE FFFFFFFF FE000007 FC000007"
    $"FC000007 FC3E07E7 FC3F07E7 FC3F8187"
    $"FC1FC187 FC1FE187 FC1FF187 FC1FF987"
    $"FC1FFD87 FC1DFF87 FC1CFF87 FC1C7F87"
    $"FC1C3F87 FC1C1F87 FC1C0F87 FC1C0787"
    $"FC3E0387 FC3E0187 FC000007 FC000007"
    $"7E000007 3FFFFFFF 1FFFFFFF 0FFFFFFE"
    /* mask (32 rows × 4 bytes) */
    $"7FFFFFE0 FFFFFFF0 FFFFFFF8 FFFFFFFC"
    $"FFFFFFFE FFFFFFFF FFFFFFFF FFFFFFFF"
    $"FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF"
    $"FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF"
    $"FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF"
    $"FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF"
    $"FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF"
    $"7FFFFFFF 3FFFFFFF 1FFFFFFF 0FFFFFFE"
};

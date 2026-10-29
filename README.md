# Notion Classic v2

**A native Notion client for compact Macs from 1989 on**


![Notion Classic starting up on System 7 and loading a page, timed like a relaunch on a real SE/30][image-1]

> [!TIP]
> **Try it in your browser at [classic.slkk.nl][1].** It's a real 68k Mac running in a browser tab, on a copy of [Infinite Mac][2] with a TCP/IP stack added. No vintage hardware needed.

Notion Classic reads and edits real Notion pages on a 68k Macintosh running System 7. It opens its own TLS 1.2 connection to `api.notion.com`, with no proxy: the encryption starts and ends on the Mac. Its 16 MHz 68030 does the cryptography itself, and that's the hard part. On a Mac SE/30, a stock mbedTLS handshake takes 49 seconds, and Cloudflare stops waiting after about 15. Notion Classic gets the first handshake down to 7 seconds, and a resumed one to under a second.

There are three documents. This README is the overview. **[The story][3]** covers the why: a 2022 side project, a three-year stall, and an AI that said to use a proxy. **[HTTPS on a 37-Year-Old Mac][4]**, in the HTTPS 68K Speedtest repo, covers the how: the handshake, the assembly, and a measured step-by-step history.

---

## What it does

- **Reads and edits Notion pages:** paragraphs, headings, to-dos, bullets, quotes, dividers and nested toggles, with formatting, links, page mentions, emoji and page icons.
- **Page library:** a tree of your pages with search, back/forward and recent pages; create, move and delete pages. The last four pages you opened are kept on disk for an hour and open without going online.
- **Edits that don't lose your text.** Formatting survives typing, Cmd-Z works like on any Mac, and failed or interrupted saves retry and finish once you reconnect. What the Mac can't fully show (an @person, a date, an equation) is marked *Read only*.
- **Fits on a floppy:** one file under 1 MB, 885 page icons included, so a 1.4 MB floppy holds it. The first release was 1.6 MB plus a folder of 1,316 icon files.
- **Debug console:** a live, timestamped log of every step, in Toolbox ticks.

<p>
  <img src="docs/img/Notion%20Emoji.png" width="49%" alt="The emoji picker">
  <img src="docs/img/Notion%20Features.png" width="49%" alt="To-dos with icons, and an open toggle">
</p>

The full v2.0 list is in the [changelog][5]

> [!WARNING]
> The connection is encrypted, but the Mac isn't: any app on it can read the prefs file with your integration secret. Use a separate integration, and share only pages you don't mind losing.

## How fast, and how I know

The diagram below shows how a Mac SE/30 opens a 30-block Notion page (29 KB of JSON), from the debug console on System 7.5.5 with Open Transport 1.3 patched by [Open Transport Bug Patcher][6]. Every step gets its typical time from several runs. Each ▼ is a request to Notion, whose server needs about 0.2 s; the rest is the Mac sending, receiving and decrypting.

- **First time:** a full handshake, which checks the certificate's signatures.
- **Returning:** resumed with the session ticket saved on disk. After 23 hours the ticket expires, and the handshake takes 5.3 s.
- **Next page:** the connection is still open, so there's no handshake at all.

Timed in one go, the same page took 18.5 s, 12–13 s and 3.2 s. A relaunch takes longer than the diagram's 7.5 s: it first draws the page's saved title and icon, and so far it has always waited about 3 s longer for the blocks than the Classic II, for a reason I haven't found yet.

![Where the time goes when the SE/30 opens a 30-block Notion page: 17.5 s the first time, 7.5 s when returning, 3.3 s for the next page][image-3]

Connecting again is **up to 60 times faster** than stock mbedTLS: in [Speed Test][41], the SE/30 connects, resumes and gets Notion's answer to one request in 0.85 s, where stock would have needed about 52 s.

Getting there took faster math (micro-ecc with the co-Z ladder from [Rivain 2011][7], Shamir's trick with wNAF, and custom 68020 assembly), a trust anchor that skips a P-384 check, keys made before they're needed, and session tickets kept on disk. The math got 4–7× faster and a first handshake 7–8×; the rest comes from not doing the same work twice.

**And on the Classic II**, the same 68030 on a 16-bit bus, the page took 25.4 s, about 12 s and 4.7 s, with a handshake of 9.0 s the first time, 0.9 s returning and 7.2 s after 23 hours.

## The short version of how this happened

In October 2022 I wrote the first version by hand, with no AI. I worked from *Macintosh C Programming by Example*, old System 7 programming books, and Anthony Super's libraries. It worked in Basilisk II, where the "68k Mac" is really a modern CPU and a handshake takes 0.2 seconds. On the real Classic II, in April 2023, the handshake never finished, so I put a small Python proxy on the Raspberry Pi. Then I hit a Toolbox wall (scrolling a page built from many TextEdit fields) and shelved the project.

In early 2026 Claude Code fixed that scroll problem, and I wanted the proxy gone. When I asked Claude to make a real SE/30 do HTTPS on its own, it told me to use a proxy. I already had one. On March 24, 2026, after two days of photographing error codes off a 9-inch screen, the SE/30 loaded a Notion page by itself.

The full version is in **[the story][8]**.

![Notion Classic on a Macintosh Classic II, a Mac SE/30, a Macintosh mini and a PowerBook G4][image-2]

## Tested on

- **Mac SE/30:** [BlueSCSI][12] for storage and network
- **Mac Classic II:** [PiSCSI][11] for storage and network
- **PowerBook G4, Mac OS 9 Classic**
- **Macintosh mini:** a Pi Zero in a Maclock case running Basilisk II ([macintosh-mini][9] by Wells Riley). I added networking so it can go online; my fork is at [SLKK-design/macintosh-mini][10].
- **Basilisk II, System 7.5.5:** works with the Ethernet port set to `slirp`
- **SheepShaver, System 8.1:** works with Open Transport's MacTCP compatibility layer

## Requirements

```text
A Macintosh with at least: 

CPU            68020
App partition  2 MB
System RAM     8 MB
OS             System 7.5
Network        Ethernet card, DaynaPORT, PiSCSI or BlueSCSI
```

> Using Open Transport 1.3? Give [Open Transport Bug Patcher][14] a run first.
> Otherwise every page waits an extra 3 seconds.

## Download

<a href="https://github.com/SLKK-design/NotionClassic/releases/latest/download/NotionClassic.sit"><img src="docs/img/Notion-icon.png" width="32" align="left" alt="NotionClassic.sit"></a>Get [NotionClassic.sit][15] (StuffIt, for the Mac itself) or [NotionClassic.zip][16].

#### Connecting to Notion

1. Create an internal integration at [notion.so/my-integrations][18] and copy its secret.
2. In Notion, share the page you want with that integration (••• → Connections). Share only what this Mac should see.
3. Copy the page ID: the 32 hex characters at the end of the page URL.
4. Launch Notion Classic, open **Connection**, and paste the secret and the page ID.

## Building

You need [Retro68][19]. The 68k mbedTLS and MacTCPHelper libraries come pre-built in `src/core/lib/`.

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<Retro68-build>/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
make -C build
```

The app ends up in `build/` as `NotionClassic.APPL`. Copy it to the Mac with its resource fork intact: on a SCSI emulator image, a floppy, or over AppleShare. A plain copy through a modern filesystem drops the resource fork, and the app won't launch.

One warning from experience: don't trust emulator timings. Basilisk II finishes a handshake in 0.2 s. A real 68030 takes 7 to 9 s.

## Sibling projects

<a href="https://github.com/SLKK-design/Claude68"><img src="docs/img/Claude-icon.png" width="32" align="absmiddle" alt="Claude 68"></a>&nbsp; **[Claude 68](https://github.com/SLKK-design/Claude68)**: a Claude chat client, and the starting point for your own 68k HTTPS app.<br>
<a href="https://github.com/SLKK-design/MacPlayer"><img src="docs/img/MacPlay-icon.png" width="32" align="absmiddle" alt="MacPlayer"></a>&nbsp; **[MacPlayer](https://github.com/SLKK-design/MacPlayer)**: Anthony Super's Spotify remote, working again.<br>
<a href="https://github.com/SLKK-design/HTTPS-68K-Speedtest"><img src="docs/img/Speedtest-icon.png" width="32" align="absmiddle" alt="Speed Test"></a>&nbsp; **[Speed Test](https://github.com/SLKK-design/HTTPS-68K-Speedtest)**: times every handshake method on your Mac.

Each app carries its own copy of the engine; `scripts/diff_core.sh` shows where Notion Classic's has drifted from Claude 68's.

## Credits

The 2022 version was written by hand. Everything since early 2026 was built with [Claude Code][21]. It wrote most of the new code, including the 68k assembly, and explained the papers as we went. I set the goals, ran each build on the real machines, read the tick logs and decided what stayed. mbedTLS runs the protocol and the cipher. What's custom is the curve assembly, an X25519 rewrite, a faster signature check, the verification cache, the trust pin and an entropy source that seeds the random generator from interrupt timing. The [deep dive][22] lists exactly what.

Built by **Lin van der Slikke**, standing on:

- [MacHTTP][23], [mbedtls-Mac-68k][24] and [MacTCPHelper][25] by Anthony Super (antscode). MacHTTP is where the HTTP client started, and his classic-Mac Spotify remote is why this project exists at all. That remote, [MacPlayer][26], now runs again on this engine.
- [Retro68][27] by Wolfgang Thaller, the 68k GCC toolchain
- [mbedTLS][28] (ARM/Mbed, Apache 2.0)
- [micro-ecc][29] (Kenneth MacKay, BSD 2-clause)
- [curve25519-donna][30] (Adam Langley / Google, BSD 3-clause)
- [http-parser][31] (Joyent / Node.js contributors, MIT), [yuarel][32] (Jack Engqvist Johansson, MIT), [gason][33] (Ivan Vashchaev, MIT)
- [Rivain (2011), *Fast and Regular Algorithms for Scalar Multiplication over Elliptic Curves*][34]
- [PiSCSI][35] and [BlueSCSI][36], for storage and the SCSI network card emulation that puts the real machines online
- [Infinite Mac][37] by Mihai Parparita, the platform behind the browser demo
- The Notion API team, for an API clean enough to parse on a Classic II

## License

My code is released under [0BSD][38]: use it however you like, no credit needed. The bundled libraries keep their own licenses (see [THIRD\_PARTY\_LICENSES][39]). The Notion page icons belong to Notion Labs, and 0BSD doesn't cover them. The emoji are my own 1-bit redraw, using Microsoft's Segoe UI Emoji designs as a starting point.

---

*A Macintosh from 1989, talking to a 2026 API over real TLS. No proxy, no helper box. [See it for yourself.][40]*

[1]:	https://classic.slkk.nl
[2]:	https://github.com/mihaip/infinite-mac
[3]:	docs/the-story.md
[4]:	https://github.com/SLKK-design/HTTPS-68K-Speedtest/blob/main/docs/modern-HTTPS-on-68k.md
[5]:	CHANGELOG.md
[6]:	https://github.com/SLKK-design/HTTPS-68K-Speedtest/tree/main/OpenTransportBugPatcher
[7]:	https://eprint.iacr.org/2011/338.pdf
[8]:	docs/the-story.md
[9]:	https://github.com/wr/macintosh-mini
[10]:	https://github.com/SLKK-design/macintosh-mini
[11]:	https://github.com/PiSCSI/piscsi
[12]:	https://bluescsi.com/
[14]:	https://github.com/SLKK-design/HTTPS-68K-Speedtest/tree/main/OpenTransportBugPatcher
[15]:	https://github.com/SLKK-design/NotionClassic/releases/latest/download/NotionClassic.sit
[16]:	https://github.com/SLKK-design/NotionClassic/releases/latest/download/NotionClassic.zip
[18]:	https://www.notion.so/my-integrations
[19]:	https://github.com/autc04/Retro68
[21]:	https://claude.ai/code
[22]:	https://github.com/SLKK-design/HTTPS-68K-Speedtest/blob/main/docs/modern-HTTPS-on-68k.md
[23]:	https://github.com/antscode/MacHTTP
[24]:	https://github.com/antscode/mbedtls-Mac-68k
[25]:	https://github.com/antscode/MacTCPHelper
[26]:	https://github.com/SLKK-design/MacPlayer
[27]:	https://github.com/autc04/Retro68
[28]:	https://github.com/Mbed-TLS/mbedtls
[29]:	https://github.com/kmackay/micro-ecc
[30]:	https://github.com/agl/curve25519-donna
[31]:	https://github.com/nodejs/http-parser
[32]:	https://github.com/jackenl/yuarel
[33]:	https://github.com/vivkin/gason
[34]:	https://eprint.iacr.org/2011/338.pdf
[35]:	https://github.com/PiSCSI/piscsi
[36]:	https://bluescsi.com/
[37]:	https://infinitemac.org
[38]:	LICENSE
[39]:	THIRD_PARTY_LICENSES
[40]:	https://classic.slkk.nl
[41]:	https://github.com/SLKK-design/HTTPS-68K-Speedtest

[image-1]:	docs/img/notion-se30-boot.gif
[image-2]:	docs/img/Notion%20on%20Macs.jpg
[image-3]:	docs/img/notion-se30-page-load.png

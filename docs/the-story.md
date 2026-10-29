# Editing Notion on a 37-Year-Old Mac, Over Real HTTPS

*How a 2022 side project, a three-year stall and some advice I ignored turned into a 16 MHz 68030 doing X25519 against Cloudflare, with an `HTTP 200` to show for it.*

There's a 1989 Macintosh SE/30 on my desk with a Notion page open. Not a screenshot, not a remote session, no Linux box doing the real work. The 68030 opens its own TLS 1.2 connection to `api.notion.com`, does the handshake itself, and draws the page on a 9-inch black-and-white screen. Tick a checkbox, fix a typo, and it saves back.

The hard part is the handshake. Stock crypto on this CPU needs **49 to 73 seconds** of math; Cloudflare hangs up after about **15**. This is how that gap got closed.

---

## A copywriter's Mac

I've been a Mac nerd since design school around 2000. In 2019 an old friend gave me her father's **Macintosh Classic II**; he'd written copy on it in the nineties. I bought the SE/30 later.

The Classic II is now the slowest machine Notion Classic supports: the same 16 MHz 68030 as the SE/30, but a 16-bit memory bus, so every 256-bit multiply costs more. If a timing holds on the Classic II, it holds.

## 2022: a network card and an interview

Years earlier I'd seen [a Hackaday post](https://hackaday.com/2018/12/21/giving-an-old-mac-spotify/) about [Anthony Super](https://github.com/antscode)'s Spotify remote for the classic Mac. He'd written MacHTTP, ported mbedTLS to 68k and written MacTCPHelper, so a System 7 app could make HTTPS requests at all. But my Classic II had no network card.

In 2022 two things got me started. [PiSCSI](https://github.com/PiSCSI), a Raspberry Pi that pretends to be a SCSI hard drive and a network card: I ordered one on October 21, 2022, and designed their new logo while I waited. And Notion's [interview with Andy Hertzfeld](https://www.notion.com/blog/andy-hertzfeld) of the original Macintosh team. Notion's quiet, blocky, black-and-white interface owes a lot to the original Mac. It seemed funny to bring it back home.

[![Andy Hertzfeld with an original Macintosh, from Notion's interview video](img/Andy%20Hertzfeld.png)](https://www.notion.com/blog/andy-hertzfeld)

I learned System 7 programming the traditional way, without AI: *Macintosh C Programming by Example*, a shelf of books, and antscode's libraries. The day I ordered the PiSCSI I wrote my first app in an emulator; the next day it printed real JSON from `api.notion.com`. By mid-November it drew a Notion page in a real System 7 window, each block in its own TextEdit field.

What I didn't appreciate yet: the emulator runs on the host CPU, where a handshake takes 0.2 seconds. And the HTTP client I'd started from never checked the server's certificate (`// BAD BAD BAD!`). Encrypted, yes. Verified, no.

When the PiSCSI arrived in April 2023, the real Classic II never got through the handshake, so I put a Python proxy on the Pi to do the HTTPS. Then I hit a wall anyone who has fought the Toolbox will recognize: **scrolling a page made of many independent TextEdit fields.** I shelved it. For three years.

## 2026: "use a proxy"

I have a love-hate relationship with AI, but at work I'd seen how good Claude had become, so early in 2026 I pointed Claude Code at the scroll problem. It fixed it.

Then I asked for the real thing: the SE/30 talking HTTPS to Notion by itself. Claude said that was a bad idea and I should put a proxy in between. I already had one. I wanted it gone.

ESP32s do HTTPS all the time, so why not a 68030? antscode's README had the answer: a 16 MHz 68030 "will take about 70 seconds - during which time the remote server will timeout." I read papers on faster elliptic-curve arithmetic and fed them to Claude. I even tried the 68882 coprocessor. No help.

The feedback loop was low-tech: build on the modern Mac, copy over, run, photograph the CRT, type the error code and tick count back into Claude. From March 22 to 24 my camera roll filled with 91 photos and 23 videos of a 9-inch screen, the later ones showing `-29312`: "the server closed the connection during the handshake."

## March 24: the second connection

By then one handshake *almost* fit. The plan was to sacrifice a connection: the first would do the expensive certificate check, cache the result and die; the second would find it in the cache and get in under the wire. It kept failing because the cache had one slot, and the second connection's own check evicted what the first had paid for. Keying the cache on the public key fixed it.

On the SE/30, Retry 1 died as planned at 907 ticks, 15.1 seconds: Cloudflare's stopwatch, caught in a log line. Retry 2 went through, and the page loaded.

I typed into Claude: *"the page was loading and then magic, it worked !!!!!"* It replied that a 68030 doing P-256 ECDH and TLS 1.3 against Cloudflare was genuinely impressive. It was TLS 1.2. I let it have the moment.

That was inside MARCHintosh with a week to spare. A shippable app took until April 19, missing Apple's 50th birthday. Then I kept going anyway.

## Making it reliable

March 24 proved the handshake *could* finish, not that it would, and on the Classic II it often didn't. Spring went into custom 68020 assembly for the one loop GCC couldn't write well; session tickets, which I enabled, saw fail, blamed on Cloudflare, and then found were fine (the bug was a function call a comment claimed existed); and saving all of it to disk so a restart doesn't pay full price.

Today a cold handshake takes 6.9 s on the SE/30 and 11.0 s on the Classic II, with the certificate check cached; a resumed one under a second on both. **The math got about 4–7× faster and a whole first handshake 7–8×; the rest came from refusing to redo it.** Every measurement is in [HTTPS on a 37-Year-Old Mac](https://github.com/SLKK-design/HTTPS-68K-Speedtest/blob/main/docs/modern-HTTPS-on-68k.md).

## The machines

- **Mac SE/30** (68030/16, 32-bit bus): the primary target, on a BlueSCSI.
- **Mac Classic II** (68030/16, *16-bit* bus): the copywriter's Mac and the slowest I support, on a PiSCSI.
- **Macintosh mini**: [Wells Riley's project](https://github.com/wr/macintosh-mini), a Pi Zero running Basilisk II in an alarm-clock case. I added networking.
- **PowerBook 12" G4**: the unsung hero: every build goes through it on its way to the 68k Macs.

Neither 68k Mac has Ethernet. The network arrives over **SCSI**: [PiSCSI](https://github.com/PiSCSI/piscsi) and [BlueSCSI](https://bluescsi.com/) emulate a Dayna SCSI/Link adapter.

> *"Pretty neat you got an https implementation working."*
> — Eric Helgeson, creator of [BlueSCSI](https://bluescsi.com/)

## Try it in your browser

Notion Classic runs at **[classic.slkk.nl](https://classic.slkk.nl)**, on my own copy of Mihai Parparita's [Infinite Mac](https://github.com/mihaip/infinite-mac). Infinite Mac can't reach the internet, so I added that: a virtual TCP/IP stack in the browser, bridged through a WebSocket-to-TCP relay on a **Raspberry Pi 5** behind a Cloudflare tunnel. The relay only moves bytes; TLS still runs inside the emulated Mac.

## A word of warning (it's still a joke)

One commenter called it *"a vibecoded TLS stack on an OS with no memory protection or file permissions."* Not entirely wrong. But "vibecoded" suggests nobody looked, so I looked, in public: the [deep-dive](https://github.com/SLKK-design/HTTPS-68K-Speedtest/blob/main/docs/modern-HTTPS-on-68k.md) separates stock mbedTLS and micro-ecc from what is mine.

The connection is encrypted; the *machine* is wide open. Anything on it can read your API token. **Don't point this at a Notion workspace you care about.** Use a dedicated, scoped token you can revoke.

## A sibling

Once the hard part was solved, the same engine could drive any modern API. So I built [**Claude 68**](https://github.com/SLKK-design/Claude68), a Claude chat client for the same machines. Yes, a client for the same Claude that told me to use a proxy. It's deliberately small: change one config file and one API file and you have an HTTPS and SSE client for another service.

Claude 68's engine also revived Anthony's Spotify remote, [MacPlayer](https://github.com/SLKK-design/MacPlayer), which had died with the web service it depended on. Now the SE/30 talks to Spotify itself.

## Here's to the round pegs

There's something about looking at a 9-inch monochrome screen from 1989, deciding it *should* be able to talk to a 2026 API, and refusing to accept that it can't.

The machine Notion is descended from deserved to run it. Now it does.

---

## Credits

Built by **Lin van der Slikke**. The 2022 version was written by hand; since 2026 it has been a collaboration with **Claude Code** (Anthropic), which wrote the assembly and chased the handshake down from a minute to a few seconds.

Standing on (full list in the [README](../README.md)): [MacHTTP](https://github.com/antscode/MacHTTP), [mbedtls-Mac-68k](https://github.com/antscode/mbedtls-Mac-68k) and [MacTCPHelper](https://github.com/antscode/MacTCPHelper) by Anthony Super, where this started (thank you, Anthony); [Retro68](https://github.com/autc04/Retro68) by Wolfgang Thaller; [mbedTLS](https://github.com/Mbed-TLS/mbedtls), [micro-ecc](https://github.com/kmackay/micro-ecc) and [curve25519-donna](https://github.com/agl/curve25519-donna); [Infinite Mac](https://infinitemac.org) by Mihai Parparita; [PiSCSI](https://github.com/PiSCSI/piscsi) and [BlueSCSI](https://bluescsi.com/); [macintosh-mini](https://github.com/wr/macintosh-mini) by Wells Riley; and the Notion API team.

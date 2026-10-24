# Editing Notion on a 37-Year-Old Mac, Over Real HTTPS

*How a 2022 side project, a three-year stall and some advice I ignored turned into a 16 MHz 68030 doing X25519 against Cloudflare, with an `HTTP 200` to show for it.*

There's a 1989 Macintosh SE/30 on my desk with a Notion page open. It isn't a screenshot or a remote session, and there's no Linux box in the next room doing the real work. The 68030 opens its own TLS 1.2 connection to `api.notion.com`, does the elliptic-curve handshake itself, and draws the page on a 9-inch black-and-white screen. You can tick a checkbox, open a toggle, fix a typo, and it saves back.

The hard part is the handshake. On this CPU, stock crypto needs **49 to 73 seconds** of math for one handshake, and Cloudflare hangs up on a handshake that takes more than about **15**. This is how that gap got closed, and how long it took me to start.

---

## A copywriter's Mac

I've been a Mac nerd since design school, the Grafisch Lyceum, around 2000. In 2019 my ex-girlfriend from those days, by then a good friend, gave me her father's **Macintosh Classic II**. He'd written copy on it in the nineties. I bought the SE/30 later.

That Classic II matters for this story because it's now the slowest machine Notion Classic supports. It has the same 16 MHz 68030 as the SE/30 but only a 16-bit memory bus, so every 256-bit multiply costs it more. If a timing holds on the Classic II, it holds.

## 2022: a network card and an interview

Years earlier I'd seen [a Hackaday post](https://hackaday.com/2018/12/21/giving-an-old-mac-spotify/) about [Anthony Super](https://github.com/antscode)'s Spotify remote for the classic Mac. I thought it was fantastic. To build it he had written an HTTP client (MacHTTP), ported mbedTLS to 68k and written MacTCPHelper, which meant a System 7 app could make HTTPS requests at all. But my Classic II had no network card, so it mostly sat there looking pretty.

In 2022 two things changed that. The first was RaSCSI, now [PiSCSI](https://github.com/PiSCSI): a Raspberry Pi that pretends to be a SCSI hard drive and, better still, a network card. I ordered one on October 21, 2022. They were making a new board and talking about a new logo, so while I waited I designed one for them. The board shipped in April 2023, with my logo on it.

The second was Notion's [interview with Andy Hertzfeld](https://www.notion.com/blog/andy-hertzfeld), one of the core members of the original Macintosh team, part of a series on the pioneers behind modern software. Notion is simple and mostly black and white, and its quiet, blocky interface owes a lot to the original Macintosh. It seemed funny to bring it back to where it came from.

I had no idea how to write a System 7 app. I learned the traditional way, without AI: sample code from *Macintosh C by Example*, books on System 7 programming in C and Pascal, and antscode's libraries. The dates are on my phone:

- **Oct 21, 2022.** The day I ordered the PiSCSI: my first app, in an emulator on a MacBook Air. The window title is "Jeeh mn eerste Macintosh 7 app" ("Yay, my first Macintosh 7 app"). The text inside admits I had no idea what I was doing.
- **Oct 22.** The Basilisk II console prints a real JSON response from `api.notion.com`, with `Server: cloudflare` in the headers.
- **Oct 24.** A round trip: the console reads Notion's Getting Started page, changes one block, and the browser next to it updates. I filmed it. That evening I drew the app in Photoshop first, the way I'd start any design job.
- **Mid-November.** My app draws a Notion page in a real System 7 window: headings, to-do boxes, bullets and quotes, each block in its own TextEdit field.

What I didn't appreciate yet: Basilisk II runs on the host CPU. In the emulator a TLS handshake takes about 0.2 seconds. And the HTTP client I'd started from never checked the server's certificate: verification was switched off, with a `// BAD BAD BAD!` comment to say so. Encrypted, yes. Verified, no.

When the PiSCSI arrived in April 2023, the real Classic II never got through the handshake (`-76`). So I put a small Python proxy on the PiSCSI's Raspberry Pi. It made the HTTPS connection to Notion and passed it on to the Mac as plain HTTP, and it worked.

Then I hit a wall that anyone who has fought the Toolbox will recognize: **scrolling a page made of many independent TextEdit fields.** I couldn't make it work, so I shelved the project and did other things. For three years.

## 2026: "use a proxy"

I have a love-hate relationship with AI. Part of me would rather it hadn't been invented, but if you don't use it, you fall behind. At work I'd seen how good Claude had become, so early in 2026 I pointed Claude Code at the one scroll problem I'd given up on.

It fixed it.

Then I asked for the real thing: an SE/30 talking HTTPS to Notion by itself, without the proxy I'd had on the Pi since 2023. Claude said that was a bad idea and that I should put a proxy in between. I already had one. I wanted it gone.

Foolishly, I figured it couldn't be that hard. ESP32s and IoT boards with very little processing power set up HTTPS connections all the time, so why not a 68030? antscode had already written down the answer, in the README of his mbedTLS port: a 16 MHz 68030 "will take about 70 seconds - during which time the remote server will timeout. So it looks like the minimum requirement is a 68040." I searched GitHub for crypto code I could learn from, read papers on faster elliptic-curve arithmetic, and fed all of it to Claude. I even tried putting the SE/30's 68882 floating-point coprocessor to work. It didn't make anything faster.

The feedback loop was very low-tech. I'd build on the modern Mac, copy the app over, run it, photograph the 9-inch screen with my phone, and type the error code and tick count back into Claude. From March 22 to 24 my camera roll filled up with 91 photos and 23 videos of a CRT. The first one says "an error of type 25" (out of memory), the next `mbedtls_net_connect returned -82`. Later ones show `-29312`, which is mbedTLS for "the server closed the connection during the handshake." One early build kept at it for 120 seconds before the server gave up on it.

## March 24: the second connection

By then the math was fast enough for a single handshake to *almost* fit. The plan was to sacrifice a connection. The first attempt would do the expensive certificate signature check, cache the result, and almost certainly die. The second would find the answer in the cache and get in under the wire.

It kept failing, because the cache had one slot. The second connection's own signature check pushed out the certificate result the first one had paid for. Keying the cache on the public key, with a slot each, fixed that.

On the SE/30, Retry 1 died as planned, at 907 ticks. That's 15.1 seconds: Cloudflare's stopwatch, caught in a log line. Then Retry 2 went through, and the page loaded.

What I typed into Claude was: *"the page was loading and then magic, it worked !!!!!"*

It replied: *"Notion on an SE/30 over HTTPS. That's genuinely impressive — a 16MHz 68030 doing P-256 ECDH and TLS 1.3 against Cloudflare."*

It was TLS 1.2. I let it have the moment.

The handshake worked on March 24, inside MARCHintosh with a week to spare. Turning it into an app I could hand to someone took until April 19, which missed Apple's 50th birthday on April 1. Then, absurdly, I kept going: opening, linking, creating and deleting pages, a page library, undo, toggles inside toggles.

## Making it reliable

March 24 proved the handshake *could* finish. It didn't prove it would finish every time, and on the Classic II it often didn't. The rest of spring went into that:

- custom 68020 assembly for the one loop GCC couldn't write well: written for this chip, to win a race against the clock, instead of left to the compiler,
- session tickets, which I enabled, saw fail, reverted while blaming Cloudflare, and then found were fine all along. The real problem was one function call that a comment claimed existed and the code didn't have,
- saving all of that to disk so a restart doesn't pay full price again.

By then I'd swapped the camera for the clipboard: copy the debug log, save it in SimpleText, and let the PowerBook turn it into a `.txt` my modern Mac could read.

Today a cold handshake takes 6.9 s on the SE/30 and 11.0 s on the Classic II, with the certificate check cached. The very first connection, which also checks the certificate's signature, takes about 8–11 s and 13–14 s. A resumed one takes under a second on both. The short version: **about 4–7× came from faster math, 7–8× on a whole first handshake, and the rest from refusing to redo it.** The long version, with every measurement, is in [HTTPS on a 37-Year-Old Mac](https://github.com/SLKK-design/HTTPS-68K-Speedtest/blob/main/docs/modern-HTTPS-on-68k.md).

## The machines

This runs on real hardware, not just in an emulator:

- **Mac SE/30** (68030/16, 32-bit bus, 1989): the primary target, on a BlueSCSI.
- **Mac Classic II** (68030/16, *16-bit* bus): the copywriter's Mac and the slowest one I support, on a PiSCSI. It's the machine I'm proudest of, because it beats Cloudflare's clock anyway.
- **Macintosh mini**: [Wells Riley's project](https://github.com/wr/macintosh-mini) that fits a Raspberry Pi Zero running Basilisk II into a Maclock alarm-clock case. I added networking so the emulator can go online and run Notion Classic.
- **PowerBook 12" G4** (OS X 10.4.6 with OS 9 Classic): the unsung hero. Every build goes through it, because it's the one machine here that speaks to both the modern world and the 68k one.

None of the real machines has an Ethernet card. The network arrives over **SCSI**: [PiSCSI](https://github.com/PiSCSI/piscsi) and [BlueSCSI](https://bluescsi.com/) emulate a Dayna SCSI/Link adapter, and MacTCP binds to it. Without that [SCSI network card emulation](https://github.com/PiSCSI/piscsi/wiki/SCSI-Network-Card), this project would have stopped at "it compiles."

> *"Pretty neat you got an https implementation working."*
> — Eric Helgeson, creator of [BlueSCSI](https://bluescsi.com/)

## Try it in your browser

You don't need a vintage Mac. Notion Classic runs at **[classic.slkk.nl](https://classic.slkk.nl)**. (Claude 68 isn't there, because every request costs API tokens.)

It's built on [Infinite Mac](https://infinitemac.org), Mihai Parparita's in-browser classic Mac. When I asked if I could use it to show the app, Mihai offered to allowlist my domain or add the app to the Infinite HD archive, and pointed out the problem: Infinite Mac has no network access, so the app wouldn't be able to load any data.

So I made my own copy of [Infinite Mac](https://github.com/mihaip/infinite-mac), which is open source, and added one. There's now a virtual TCP/IP stack inside the emulator: ARP, DHCP, DNS and TCP synthesized in the browser, bridged through a WebSocket-to-TCP relay to the internet, all self-hosted on a **Raspberry Pi 5** behind a Cloudflare tunnel. The relay only moves bytes. TLS still runs inside the emulated Mac. So `classic.slkk.nl` is a 68k Mac in a browser tab, making encrypted API calls through Cloudflare to an API behind Cloudflare.

## A word of warning (it's still a joke)

Putting it online drew the criticism you'd expect. The sharpest version, from one commenter: *"a vibecoded TLS stack on an OS with no memory protection or file permissions."*

It's not entirely wrong. None of my glue code is audited, and the machine has no memory protection. But "vibecoded" suggests nobody looked, so I looked, in public: the [deep-dive](https://github.com/SLKK-design/HTTPS-68K-Speedtest/blob/main/docs/modern-HTTPS-on-68k.md) separates what is stock mbedTLS and micro-ecc from what is mine. That accounting is half the reason it exists.

So, to be clear: this was built for fun. The connection is encrypted, but the *machine* is wide open. Anything on it can read your API token, and a stray bug could in principle make an API call you didn't ask for. **Don't point this at a Notion workspace you care about.** Use a dedicated, scoped token you can revoke.

## A sibling, and why there's a boilerplate

Once the hard part was solved for Notion (a full TLS handshake on a 68k Mac, inside Cloudflare's timeout), it was obvious the same engine could drive any modern API. So I built [**Claude 68**](https://github.com/SLKK-design/Claude68), a Claude chat client for the same machines that streams its replies word by word.

Yes, a client for the same Claude that told me to use a proxy.

Claude 68 is deliberately small, and I built it as a starting point for whoever does the next one. Change one config file and one API file and you have an HTTPS and SSE client for a different service on a 16 MHz 68030. The [deep-dive](https://github.com/SLKK-design/HTTPS-68K-Speedtest/blob/main/docs/modern-HTTPS-on-68k.md) covers how little it takes.

The first app it went into was the one that started all this. Anthony's Spotify remote, [MacPlayer](https://github.com/SLKK-design/MacPlayer), had stopped working: its login and album covers went through his web service at 68k.io, and that domain doesn't exist any more. On the same engine, the SE/30 now talks to Spotify itself and decodes the album covers on the Mac.

## Here's to the round pegs

There's a line in an old Apple ad about the round pegs in the square holes. I'm not claiming to change anything. But there's something about looking at a 9-inch monochrome screen from 1989, deciding it *should* be able to talk to a 2026 API, and refusing to accept that it can't. It feels like the same daft impulse.

The machine Notion is descended from deserved to run it. Now it does.

---

## Credits

Built by **Lin van der Slikke**. The 2022 version was written by hand. Since 2026 it has been a collaboration with **Claude Code** (Anthropic), which read the papers with me, wrote the assembly, and chased the handshake down from about a minute to a few seconds, across many, many attempts.

Standing on (full list in the [README](../README.md)):

- [MacHTTP](https://github.com/antscode/MacHTTP), [mbedtls-Mac-68k](https://github.com/antscode/mbedtls-Mac-68k) and [MacTCPHelper](https://github.com/antscode/MacTCPHelper) by Anthony Super (antscode), where this started. Thank you, Anthony.
- [Retro68](https://github.com/autc04/Retro68) by Wolfgang Thaller
- [mbedTLS](https://github.com/Mbed-TLS/mbedtls), [micro-ecc](https://github.com/kmackay/micro-ecc), [curve25519-donna](https://github.com/agl/curve25519-donna)
- [Infinite Mac](https://infinitemac.org) by Mihai Parparita
- [PiSCSI](https://github.com/PiSCSI/piscsi) and [BlueSCSI](https://bluescsi.com/)
- [macintosh-mini](https://github.com/wr/macintosh-mini) by Wells Riley
- The Notion API team, for an API clean enough to parse on a Classic II

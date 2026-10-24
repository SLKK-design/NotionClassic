# What's New in Notion Classic

## v2.0 — October 2026

This is a big update over the first release (April 2026). The app is faster,
more stable and a lot more complete, with a focus on one thing above all:
your writing should never get lost or mangled on its way to Notion. Here's
what changed, in plain language.

### Browsing your pages

- **A real page library.** There's now a proper window that shows your pages
  in a tree, so you can see how everything is organised and jump straight to
  any page.
- **Search.** Type to filter the page list instead of scrolling through it.
- **Back and forward.** The app remembers where you've been, so you can move
  through pages like a web browser.
- **Recently opened pages.** A running list of the pages you've visited, so you
  can jump back to one without hunting for it.
- **Pages open instantly.** When you revisit a page, its title and icon show
  up right away from a saved copy while the latest version loads in the
  background — no more staring at a blank screen.
- **Create and delete pages** directly from the library. Deleting a page
  with subpages first tells you how many there are, since they go too.
- **Move pages into and out of other pages** from the library. Notion can't
  move a page to the very top level, so for that one the app tells you to
  do it in Notion.
- **Clearer triangles in the library.** A black triangle means a page may
  have pages inside it; it turns gray once the app has checked and found
  none.

### Writing and editing

- **Better lists and checkboxes**, including correct indentation when they're
  inside toggles.
- **Page icons.** Pick an icon (Notion's own icon set or an emoji) for a page,
  and child pages show their icon too.
- **Saving is clearer.** The app asks whether you want to save unsaved changes
  before you reload, navigate away, or quit.
- **Home and End jump to the start or end of a block**, and Enter works like
  Return. On an extended keyboard these keys used to type invisible characters.
- **Copy and Paste from the Text menu** work in the page, not just Cmd-C and
  Cmd-V.

### Your text is safer

- **Formatting survives editing.** Fixing a typo used to strip a block's links,
  inline code and text colours. Now they stay.
- **Changing a block's type can't lose it any more**, even when the network
  drops halfway.
- **Content the Mac can't fully show is protected.** A block with an @person,
  date or equation is marked **Read only**, and one too big to load is marked
  **Too long**, so an edit can never overwrite the parts you can't see.
- **Failed saves are retried.** If Notion refuses a save you see **Not saved**
  and the app tries again; while the network is gone it says **No network** and
  finishes by itself once you reconnect. One refused block no longer holds up
  the rest.
- **Deleting is undone if Notion doesn't confirm it**, so a block never
  vanishes on the Mac while it still exists in Notion.
- **Tabs in code blocks and spaces before emoji** are kept when you save.
- **Undo works like on any Mac.** Cmd-Z undoes your last bit of typing, paste,
  cut or style change, and pressing it again redoes it. It also works after
  you've clicked somewhere else, and still finds the right block when blocks
  above it move.
- **Return keeps your formatting.** Bold, italic and links stay on the right
  words when you split a block, and when you undo.
- **A read-only page says so straight away.** Typing, ticking a checkbox or
  picking an emoji there shows **Read only** instead of making a change that
  can't be saved.

### Longer text

- **Blocks can hold up to 2,047 characters** (was 511).
- **Even longer text is split into several fields** on the Mac and saved back as
  one block, so nothing is cut off.
- **Typing at the end of a full block continues in a new block** below.
- **Split text can be retyped and indented** as a whole, like any other block.

### Toggles

- **Toggles work properly.** You can expand and collapse them, nest content
  inside, move items in and out, and the headings can be bold.
- **Changing a toggle's type keeps its contents.** Turning it into a paragraph
  moves its children out to sit right under it; turning it into another toggle
  keeps them inside. They used to end up in the trash.
- **Moving and indenting a toggle takes its contents along**, and a closed
  toggle no longer needs to open just to be moved.
- **Reordering blocks inside a toggle is saved** to Notion.
- **Toggles with many children** reopen complete instead of with some missing.
- **Moving in and out of toggles is smoother.** Each press goes one level in
  or out, with its own animation. Moving down into an open toggle puts the
  block at the top of its contents, and a block can now move past a toggle
  that has children.
- **Opening a toggle for the first time** redraws the page once instead of
  twice.

### Links, mentions and emoji

- **Emoji.** You can pick emoji from a searchable grid, insert them into your
  text, and copy and paste them between blocks. They now sit correctly on the
  line.
- **Typing a space after a link** steps out of it, and editing a link whose text
  is the web address updates the link itself.
- **Page mentions are kept and stay in one piece.** Typing inside one puts
  the text after it, and Backspace removes the whole mention in one go.
- **Emoji at the start of a block** and the emoji picker in nearly full blocks
  behave correctly.
- **Emoji in headings and the page title** no longer leave a wide gap before
  the text that follows.
- **Deleting an emoji no longer changes the ones after it.** Backspace, cut
  or paste over one emoji used to turn every later emoji into its neighbour.
- **An emoji goes in one Backspace** (or Delete), instead of first eating the
  invisible spaces after it.
- **Every emoji shows up.** Newer emoji the Mac doesn't have a picture for
  now show the closest one it does (🥶 shows as 😬, flags as ⛳), and skin
  tones fall back to the plain version. Notion still gets the real emoji.
  Text arrows (← ↑ → ↓) show as arrows instead of a box.

### Look and feel

- **Cleaner menus**, reorganised into sensible groups (Notion, Page, Text,
  Format, Insert), with keyboard shortcuts.
- **Full-width mode** for reading on small screens.
- **Smoother drawing.** Lots of flicker, flashing, and "ghost" leftover pixels
  have been fixed — when opening and closing pickers, scrolling, switching
  pages, and moving other windows around.
- **Move blocks** up and down with a small animation.

### Faster and lighter

- **Secure connections are dramatically faster.** Connecting to Notion over
  HTTPS used to be slow on an SE/30 and hit-or-miss on a Classic II. Custom-tuned
  code brings a fresh connection down to seconds and a reconnect to under one,
  so the app now loads pages reliably even on a Macintosh Classic II.
- **The app starts up leaner and uses less memory**, which matters a lot on
  machines with little RAM.
- **The app is now a single file.** Icons, emoji and pictures are built in,
  so there's no assets folder to copy, and the whole thing fits on one
  high-density floppy: under 1 MB, where the first release was a 1.6 MB app
  plus a folder of 1,316 icon files. The 885 page icons are packed into
  112 KB and the app reads only the one a page needs. Icons with long names
  now show up too.
- **Snappier typing in long blocks**, and much faster deleting and opening of
  toggles with lots of children.
- **Fewer network requests** when moving or retyping toggles.
- **Leaner buffers.** Dropping a duplicate copy of every block's text and
  trimming scratch memory saves over 100 KB — which pays for much of the room
  the longer blocks need.
- **Closed toggles take far less memory.** Their contents are packed tightly
  while closed, which gives back up to about 160 KB, and toggles of any size
  can now be kept ready to reopen instantly.
- **Saving long blocks uses much less memory**, because each save is now
  written out in one go instead of through several temporary copies.

### Connection and stability

- **Stronger keys for the secure connection.** The random numbers behind each
  connection's keys came from the Mac's clock, which someone who recorded your
  traffic could have guessed. They now come from the timing of the Mac's own
  interrupts and a seed that every launch updates. Starting up takes about a
  second longer. If you used a build from May to October 2026, revoke its
  Notion token and make a new one.
- **On Open Transport 1.3, run [Open Transport Bug Patcher](https://github.com/SLKK-design/HTTPS-68K-Speedtest#open-transport-bug-patcher) once.** It's a
  separate app that fixes Open Transport's handling of Cloudflare's TCP window
  scale. Without it every page waits 3 to 4 seconds.
- **No more "Connection Error" after the Mac sat idle** — the app quietly
  reconnects instead.
- **A save no longer hangs when the network drops mid-way.** If the
  connection died while Notion's answer was on its way, the app used to wait
  for it forever. Now it gives up after 30 seconds, reconnects and finishes
  the save once the network is back.
- **Fixes in the secure-connection code** that could corrupt memory or fail a
  handshake in rare cases, and a network address that could go stale after a
  restart.
- Fixed a number of crashes (switching pages, closing dialogs, network
  start-up problems, and a few low-memory situations).
- Fixed text and emoji copy/paste, page reordering, and several edge cases that
  could corrupt content or show the wrong thing.

### Under the hood

The entire codebase was reorganised from one enormous file into a clean,
modular structure, and went through a thorough clean-up pass for safer string
handling, clearer naming, and less duplicated code. None of this is visible
when you use the app, but it makes everything above more reliable and easier to
keep improving.

---

*Curious how a Mac from 1989 talks to a 2026 API over real HTTPS? Read [the
story](docs/the-story.md), or try it in your browser at
[classic.slkk.nl](https://classic.slkk.nl).*

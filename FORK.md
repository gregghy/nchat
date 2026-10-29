# gregghy/nchat — custom fork

Personal fork of [d99kris/nchat](https://github.com/d99kris/nchat) — a
terminal chat client for Telegram, WhatsApp and Signal — tuned for a
kitty + Hyprland setup on Arch.

- **Work happens on branch `custom`** (`master` tracks upstream).
- Dev repo lives at `~/progetti/nchat`; packages are built from it via
  `~/.cache/yay/nchat-git/PKGBUILD` whose `source=` points at
  `file:///home/gregghy/progetti/nchat`.
- All the config referenced below (`nchat/key.conf`, `kitty/*.conf`,
  `hyprland.lua`, `open-media.sh`, …) lives in the tracked dotfiles repo:
  [github.com/gregghy/.config](https://github.com/gregghy/.config)

## Features added on top of upstream

### Inline image previews (kitty graphics protocol)

Photos, screenshots and static `.webp` stickers render inline in the
chat history using kitty's graphics protocol:

- Messages with image attachments emit kitty image data (chunked
  `ESC_G ... \` transfers) plus a placement action; the history view then
  draws rows of *unicode placeholder cells* (`U+10EEEE` + combining
  row/col/id diacritics) so ncurses owns the cells — scrolling,
  chat-switching and redraws just work, no manual re-draw logic.
- The image id is smuggled to kitty through the cell's foreground color:
  ids get an ncurses color pair (`wattr_set`, pair index 1000+, **not**
  `COLOR_PAIR()` which masks to 8 bits) whose palette entry `16..255`
  equals the id's low byte (`38;5;N` SGR). Pair ≥256 requires
  `wattr_set` since `chtype` truncates pair indices.
- Exact aspect ratio from `TIOCGWINSZ` (real cell pixel size) rather than
  assuming 1:2 cells.
- `stb_image` for raster decode, `libwebp` fallback for `.webp`
  (Telegram sticker packs). Animated `.tgs`/Lottie stays a text
  attachment — it's JSON animation, not raster.
- `Ctrl+O` / click still opens media externally via
  `~/.config/nchat/open-media.sh` (`kitten icat` wrapper).

### Two-column layout

`uiview.cpp` reworked: the contact sidebar spans the **full terminal
height**; top bar, history, status, entry and help live in the right
column only — the input field sits directly under the chat it belongs
to.

### Entry restyle

Input line shows a `❯` accent prompt (gruvbox blue), continuation lines
indent under the text column, and an empty entry shows a dim `Message`
hint. Colors configurable via `color.conf`:
`entry_prompt_color_fg` (default `0x83a598`),
`entry_hint_color_fg` (`0x928374`).

### Focus-mode arrows

`←` / `→` pick what `↑`/`↓` do:

- `→` = chat focus — `↑`/`↓` scroll through messages (message selection)
- `←` = list focus — `↑`/`↓` navigate the contact list
- While typing, `←`/`→` remain text-cursor keys and `↑`/`↓` keep editing
- `Ctrl+↑` (`select_msg`) still jumps straight into message selection

### Mouse

- `mousemask(ALL_MOUSE_EVENTS)` enabled — wheel scrolls the **chat
  history** 3 lines per notch (previously kitty translated wheel into
  arrow keys and it changed contacts instead)
- Wheel also scrolls file/contact/emoji dialogs
- **Click a `>` quote line → jumps to and highlights the original
  message.** The history view records a row → (msgId, isQuote) map
  during `Draw()`; `BUTTON1_PRESSED` inside the history rect resolves
  the row and fires `FindMessageRequest`.
- Side effect: terminal text selection inside nchat needs `Shift+drag`
  (standard kitty override for mouse-enabled apps)

### File picker

`Send File` (Ctrl+M) sorts regular files **newest-first by mtime**
(`stat()` on each `DirEntry`); directories stay alphabetical on top.

### Dialogs

`Esc` cancels every dialog — file picker, emoji picker, find, goto-chat,
message dialogs — via `KEY_CTRL[` in all three dialog base classes.
`set_escdelay(100)` keeps it snappy.

### WhatsApp LID names

WhatsApp's privacy LIDs (`xxx@lid`) fall back to the message's
`PushName` (public display name) when no lid→phone mapping has synced,
instead of showing the raw id. Chats with no traffic and no mapping
still stay `@lid` until something arrives.

## Keybindings (`~/.config/nchat/key.conf`)

| Key | Action |
|---|---|
| `←` / `→` | arrow target: contact list / chat history |
| `↑`/`↓` | navigate whichever side is focused |
| `Enter` / `Shift+Enter` | send / newline without sending |
| `Esc` | close dialogs |
| `Ctrl+H` | page through command list in the help bar |
| `Ctrl+M` | attach file (kitty maps it to `Esc m` — Ctrl+M *is* Enter) |
| `Ctrl+F` | find text in current chat |
| `Ctrl+E` / `Ctrl+N` | emoji picker / goto chat |
| `Ctrl+V` | paste (needs `wl-clipboard` on Wayland) |
| `Ctrl+O` / `Ctrl+W` | open attachment / open link |
| `Ctrl+↑` | enter message selection (then type = quote-reply) |
| `Ctrl+Shift+S` | react to selected message |
| `Ctrl+Shift+R` | forward selected message |
| `Ctrl+Shift+P` / `+Q` | pin message / jump to quoted |
| `Ctrl+Shift+A` / `+D` | archive / delete chat |
| `Ctrl+Shift+U` / `+N` / `+2` | unread chat / add contact / mention |
| `Ctrl+Shift+E` / `+T` / `+I` | ext edit / ext call / autocomplete |
| `Ctrl+←→` / `Ctrl+Del` / `Ctrl+Bksp` | word nav / word delete |
| `Ctrl+G` / `Ctrl+L` / `Ctrl+Y` | toggle help / list / emoji |

## Kitty integration

nchat's Alt-letter bindings are dead under Hyprland (Alt is the global
modifier — `Alt+C`/`Alt+Q` would literally close the chat window).
Solution: nchat's bindings stay as `ESC<byte>` sequences, and kitty
emits those same bytes for `Ctrl+Shift+<key>` via `send_text` — but
**only inside the dedicated nchat window**.

- `~/.config/kitty/kitty.conf` (global):
  `map ctrl+m send_text all \x1b\x6d` (attach),
  `map shift+enter send_text all \x1b\r` (linebreak),
  `map ctrl+c copy_or_interrupt`, `copy_on_select clipboard`
- `~/.config/kitty/nchat.conf` (nchat window only): all the
  `ctrl+shift+<key>` → `ESC<byte>` maps above
- Launch (hyprland.lua `Alt+M`):
  `kitty --class nchat --config kitty.conf --config nchat.conf -e nchat`

## Build & install

```bash
cd ~/progetti/nchat
# ...edit, commit...
cd ~/.cache/yay/nchat-git && makepkg -e -f
sudo pacman -U ~/.cache/yay/nchat-git/nchat-git-*.pkg.tar.zst
git -C ~/progetti/nchat push      # back up to the fork
```

`-e` (noextract) is required — plain `makepkg`/`yay` re-clones the
source and discards the working tree (harmless now that the source IS
the real repo, but `-e` also keeps the incremental build cache).

## Syncing upstream

```bash
cd ~/progetti/nchat
git fetch upstream
git rebase upstream/master        # on branch custom
```

## Known limitations / TODO

- `.tgs` animated stickers: Lottie/JSON — renders as attachment line
- WhatsApp chat history: served from local cache only; no server
  backfill exists in whatsmeow's sync model (Telegram backfills fine
  via TDLib `getChatHistory`)
- Contact-list avatars: `ChatInfo` carries no photo field — needs
  protocol-layer plumbing (tdlib `chat.photo` download → model → list)
- Click on non-quote message rows does nothing yet (row→msg map exists
  and could drive click-to-select / click-image-to-open)

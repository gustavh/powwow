# Changes in powwow

The user-visible changes in each release; those up to 1.3.2 are in
`ChangeLog.old`.

## Changes from 1.3.2 to 2.0.0 (unreleased)

### Launcher

* `powwow-launcher`, a desktop program built with Qt 6 and CMake in
  `launcher/` (`make launcher`), lists the MUDs, one definition file
  each, in an order of your own, adds, imports, changes, duplicates and
  removes them, edits the settings for all of them, and starts powwow
  for the one picked in a terminal: Windows Terminal on Windows, iTerm2
  or Terminal on macOS, and that of `TERMINAL`, or one found,
  elsewhere.
* The Windows installer includes the launcher, which its shortcuts and
  the `.powwow` files open instead of powwow.
* On macOS, `make macos-dmg` makes a disk image with `powwow.app`: the
  launcher, with powwow, its help and its manual.
* **What's new**, on the **About** page of the launcher, shows the notes
  of this release, its section of this file, which powwow installs as
  `release-notes.md` and `powwow --get release-notes` finds.

### User-wide settings

* powwow reads a user-wide settings file, `powwow.conf`, before the
  definition file, for the settings that are the same for all MUDs:
  * It is in `~/Library/Application Support/powwow` on macOS,
    `%APPDATA%\powwow` on Windows, and `$XDG_CONFIG_HOME/powwow`, by
    default `~/.config/powwow`, elsewhere.
  * With `--config`, as the launcher starts powwow, the user-wide
    settings (the options `editor`, `pager`, `help-pager`, `shell` and
    `save`, `#secret`, `#hilite` and `#editsuffix`) are saved there
    instead of in the definition file; `--config=FILE` uses another
    file, and `--no-config` none.
  * A `#hilite` typed or in the definition file is the MUD's own, which
    wins over the user-wide one.
* `#setvar keepalive` in the user-wide settings file sets the keepalive
  of all MUDs, which the **Settings** page of the launcher sets. Typed
  or in a definition file, it sets the MUD's own, which **Advanced…**
  in the **Add** and **Edit** dialogs of the launcher sets, until
  `#setvar keepalive=default` drops it.
* `powwow --get NAME`, `--set NAME=VALUE` and `--list-settings` read
  and set the settings of the user-wide settings file and of a
  definition file without a session, as the launcher does.

### Completion and Unicode input

* Tab completes file names that have spaces in them.

### Commands, attributes and messages

* `#option save`, on by default, saves the definition file on exit;
  `#option -save` leaves it as it is.
* `#hilite` without an attribute shows it, and `off` or `none` turns it
  off; without `input` or `messages`, `off`, `none` and `default` are
  for both.
* By default, typed text is `bold` and the messages of powwow are
  `italic Blue`.
* powwow starts without the notice of the warranty and the license,
  which `#help warranty` and `#help copyright` show.
* `#setvar <name>=default` sets `buffer`, `editmem`, `lines` and `mem`
  to their defaults, and `lines` to the height of the terminal;
  `#setvar` marks those that have them with `(default)`.
* `#save` leaves out the settings that have their defaults: `#delim`,
  `#groupdelim`, `#setvar mem` and the options, as it did
  `#setvar editmem`; and `#load` gives those that a file does not set
  their defaults, also `#host`, `#init` and `#setvar buffer`, instead
  of keeping those of the file loaded before.
* `#setvar editmem`, 16777216 by default, limits the length of the
  texts to edit or view that the MUD sends, and of the edited texts,
  instead of `#setvar mem`, 1048576 by default, as MUME sends longer
  ones.
* `#editsuffix` sets the suffix of the temporary file of a text the
  MUD sends to edit or view, which is named after it, from its title,
  so that the editor can tell what it is: by default, `Edit mudlle
  file` gives `.mud`, a file name at the end of the title its
  extension, such as `.sh`, and other titles `.txt`.
* `#capture --telnet <file>` writes the bytes the MUD sends, with the
  telnet commands, such as IAC GA after a prompt.
* `#capture --pcap <file>` writes the bytes the MUD sends and those
  powwow sends, each read and write a packet with the time, for
  Wireshark and tcpdump.
* A telnet GA after a prompt ends it, so that text after it with no
  newline before it, such as MUME's prompt after the prompt of its
  pager, starts a line of its own.
* CHARSET: powwow sends its REQUEST with its WILL CHARSET, as soon as
  the MUD sends DO CHARSET, without waiting for WILL CHARSET, which
  saves the MUD a round trip.
* Telnet replies to what the MUD sends at once, such as the window
  size, the terminal type and CHARSET, go out in one packet, not in one
  each.

### Platforms and building

* Windows using Cygwin is supported, along with WSL2. Cygwin builds
  powwow without `#module`.
* `cygwin/bundle.sh` and `cygwin/powwow.iss` make a Windows installer
  of powwow built on Cygwin, which runs without Cygwin installed (see
  `README`).
* `powwow --windows`, as the installer runs it, takes file names and
  commands as on Windows:
  * A file name after `<` and a command after `!` keep their
    backslashes, which separate folders in the names of definition
    files too.
  * `%NAME%` in file names is replaced by the value of the environment
    variable, the way messages and `#help` write environment variables
    then.
  * The editor and the pager run as `#!` runs its commands, followed by
    the Windows path of the file, and are Notepad by default, or `nano`
    in an ssh session.
  * The shell is `powwow-cmd`, which runs the commands with `cmd`; as
    for `sh`, a `\\` in double quotes in those commands and in those of
    `#!` is one `\`, so that `"C:\\Program Files\\..."` is the same in
    an `#alias`.
  * Tab completes file names in any case, with a `\` after folders.
  * Edited texts are sent without the byte order mark and the CR of CR
    LF that editors with Windows settings write.
  * `#spawn` is not available, as Windows programs cannot talk through
    its connection.
  * A file name that Windows does not allow, which Cygwin would turn
    into another, such as `c:` for `C:\`, is an error, for `#capture`,
    `#movie`, `#record`, `#write`, `#file`, `#save` and `#load`.

### Bug fixes

* `#setvar mem` must be 0 (zero), no limit, or from 4096, and
  `editmem` 0 or from 65536, up to 2147483647; a smaller value was
  ignored, and a larger one cut to 2147483647, without a word.
* Lines and keys sent to a MUD that speaks telnet end in CR LF, as
  telnet wants, instead of LF, and a CR without LF after it is CR NUL;
  `#rawsend` and the edited texts of MPI send theirs as they are.
* Fixed `#isprompt` without a length never drawing a prompt that has no
  newline after it.
* Fixed `#zap` of a connection with TLS waiting forever for a MUD that
  does not answer the end of the session; it waits for a second at
  most.
* Fixed loading a definition file reporting what its lines do after an
  `#option +info` in it, such as the last of the lines of `#option`
  that `#save` writes, and what is added to an older file;
  `#option info` takes effect at the end of the file.
* Fixed the cursor moving rows too high on a long input line after
  lines from the MUD wider than the screen, such as one that came in
  more than one read, and after such `#print` text and messages.
* Fixed `#option wrap` wrapping the lines of a connection that is not
  the default one as if the label before them, such as `##chat>`, took
  no room.

VagaFileManager (AmigaOS 4.1, ReAction)

Version 1.1 build 99 (26.11.2026)

A modernization project for AmigaOS 4.1.

About the Project
The old OS3 file management system was functional back in the floppy disk era, and despite the improvements introduced over time, as the number of files grew it became increasingly difficult to launch applications and manage files efficiently.
It is no coincidence that many file managers (and even full Workbench replacements and upgrades like DOpus Magellan and Scalos) were created on Amiga to allow dynamic filesystem navigation alongside icon views.
VFM is my personal attempt to fill this gap and put an end to having dozens of open windows where you constantly have to arrange icons, snapshot positions, resize windows, and so on. MorphOS found an excellent solution with Ambient, whereas OS4 (suffering from a state of near-abandonment by its parent company) has been left behind.
To keep the core program lightweight and fast, extra features are plugins, written in Python or ARexx, loaded only when they are used.
Note: I am not a hardcore developer, and I made use of LLMs during the development of this project.

Project page: https://github.com/VagaPPC/VagaFileManager

Donations
VFM is free.  If you like it, you can support it on Ko-fi:
https://ko-fi.com/vagappc
My only goal is to recover the money I have spent on the project.  If
donations ever go beyond that, the excess will be passed on to other
Amiga projects.

Features
- Fast browsing of the whole filesystem in one window, with icons or a
  detailed list; view, sort order and icon size remembered per drawer.
- Split mode: a second pane for copying and moving, inspired by DOpus.
- Copy, move, delete, duplicate and rename with pause, cancel, update
  (copy only newer files) and Workbench-style error requesters.
- Workbench-style drag and drop, also to other programs.
- Bookmarks, quick filter with AmigaDOS patterns, automatic refresh.
- WB2VFM commodity: VFM windows instead of the Workbench drawer windows.
- Fast network drawers (Samba and other handlers listed in NETHANDLERS).
- Plugins: Find, Grep, LHA/LZX/tar.gz archives, and mounting of Samba,
  SSH, FTP/SFTP and NFS shares found on the local network.
- AISS images, ARexx port, translated into English, Italian, French,
  German, Polish, Spanish and Greek.

Requirements
- AmigaOS 4.1.
- Optional: AISS (toolbar and menu images from TBImages:), Python (for
  the plugins), the network handlers needed by the Mount plugins.

Installation
Copy the VFM drawer and its icon VFM.info wherever you like and start
VagaFileManager.  Keep Catalogs, icons, Plugins and Docs next to the
program.  Start WB2VFM (or put it in SYS:WBStartup) to use VFM instead of
the Workbench drawer windows.

Documentation
- Docs/<language>/VagaFileManager.guide: how to use VFM, in every
  language.  Program > Guide..., F1 or the Help key opens it in the language
  of the system.
- Docs/Developer.guide: writing plugins, the command channel and ARexx
  port, building and translating VFM (English).
- CHANGELOG.md: what changed in every build.

Feedback & Contributions
Any suggestions, bug reports, ideas, and constructive criticism are very welcome! Feel free to open an Issue or start a Discussion in this repository:
https://github.com/VagaPPC/VagaFileManager

License
-------
VagaFileManager is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program.  If not, see <https://www.gnu.org/licenses/>.

The full license text is in the LICENSE file, which is also copied into
the VFM/ distribution folder.

Note: FileBrowser.c and FileBrowser.h are a separate work by OpenAmiga.org
("Listbrowser gadget with cell mode") released into the public domain.
They are used with one small VFM addition: the read-only
`FILEBROWSER_Columns` get attribute, used for keyboard navigation in icon
view.  The rest is unchanged.

# Tidy v1.1.6

**A visual cleaner for Homebrew packages, apps and system junk on macOS.**

Tidy shows everything you installed from the terminal, what each package does, and what it is connected to. When you remove something, it removes the related leftovers too, with a clear checklist so you stay in control.

Built with C++17 and Qt 6.

<!-- Add a screenshot after your first build:
![Tidy screenshot](docs/screenshot.png)
-->

## Features

- **Homebrew browser**: every formula and cask you have installed, with description, version, size and whether you installed it yourself or it arrived as a dependency.
- **Dependency graph**: see what a package depends on and what needs it. Click a node to jump to it.
- **Clean uninstall**: removing a formula also offers its cached downloads and dependencies nothing else needs. Removing a cask or app also finds its leftovers in `~/Library` (Application Support, Caches, Preferences, Containers, Saved State, Logs).
- **Orphan filter**: instantly list dependencies that are safe to remove.
- **System Junk**: caches, logs, Trash, Xcode DerivedData, npm / Cargo / Gradle caches and old device backups, each with its size.
- **Large Files finder**: files over 500 MB in your home folder, biggest first.
- **Extras**: update and upgrade Homebrew, clear the Homebrew cache, export a Brewfile, reveal in Finder, open a package homepage, free-space indicator.

## Safety

- Every removal shows a checklist and asks for confirmation first.
- Apps and their leftovers are moved to the **Trash**, so you can recover them.
- Homebrew uninstalls and emptied cache folders are deleted permanently.
- A package that other installed packages depend on cannot be removed until those are removed.
- Leftovers matched only by name (not by exact bundle ID) are unticked by default.
- Tidy never touches `/System`, and it only scans your home folder.

## Requirements

- macOS 12 or newer
- [Homebrew](https://brew.sh) (the app still works without it, but only shows Applications, System Junk and Large Files)
- Xcode Command Line Tools: `xcode-select --install`
- CMake and Qt 6: `brew install cmake qt`

## Build and run

```bash
git clone git@github.com:YOUR-USERNAME/Tidy.git
cd Tidy
cmake -B build -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build
open build/Tidy.app
```

To make a copy that runs on Macs without Qt installed:

```bash
$(brew --prefix qt)/bin/macdeployqt build/Tidy.app
ditto -c -k --keepParent build/Tidy.app Tidy.zip
```

The app is not code-signed. On first launch, right-click it, choose **Open**, then confirm.

## Usage

1. Pick a section in the sidebar: Formulae, Casks, Applications, System Junk or Large Files.
2. Select an item to see its details and connections.
3. Review the **What will be removed** checklist and untick anything you want to keep.
4. Click **Remove Selected** and confirm.

Some casks use installer packages that need an admin password to uninstall. Tidy cannot prompt for one, so it will show the error and you can run `brew uninstall --cask --zap <name>` in Terminal instead.

## Project layout

```
Tidy/
├── main.cpp          # the whole application
├── CMakeLists.txt    # build configuration
├── README.md
├── LICENSE
└── .gitignore
```

## Roadmap

- [ ] Run scans and uninstalls in background threads
- [ ] Per-package "last used" information
- [ ] Scan `~/.config` and dotfiles for leftovers
- [ ] Welcome screen on first launch
- [ ] Code signing and notarization

## Contributing

Issues and pull requests are welcome. For larger changes, open an issue first to discuss what you would like to change.

## Disclaimer

Tidy deletes files when you tell it to. Keep a backup (Time Machine) and read the checklist before confirming. The authors are not responsible for data loss.

## License

MIT, see [LICENSE](LICENSE).

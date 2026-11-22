# Online play with re-psn: quick start

The game's own Online Battle can run against a [re-psn](https://github.com/undecuple/re-psn) server instead of PSN.
re-psn is a small matchmaking server that one player, or anyone else, hosts.
- **Through the server:** rooms, search and the player list.
- **Peer to peer:** the match itself goes directly between the players' games. If a direct link fails, it goes through
  the server's relay, but only when the host turned the relay on.

Online is experimental. **It is off after install** until you choose a server: the settings window's **Online** tab, re-psn, then a
server you type or find with **Find on LAN** (this project names no server), and Save. The tab's rollback box (on by default) turns rollback on
for Online Battle (the game's Friendly Match room pairs the players, rollback runs the fight); see [TESTING_ONLINE.md](TESTING_ONLINE.md).
A test build offers its own server instead: one click on **Use** *server name* **(TEST BUILD)** at the top of the **Online** tab turns it
on, with rollback. Nothing changes before that click. An install that already had online settings keeps them. To host your own server instead,
follow the steps below. It needs the installer's only tree (`--tree current`; the earlier legacy tree is retired, tag `legacy-v15`), which builds the port on ps3recomp v0.19.0; see [installer/README.md](../installer/README.md#online-battle-experimental)).

## 1. One player hosts a server

1. Download re-psn from its [releases](https://github.com/undecuple/re-psn/releases). There's a Linux build and a Windows
   zip; they're still pre-releases.
2. Start it as its README says. Add `-relay` if players may be behind routers that block direct links.
3. Open TCP and UDP port 36100 on the server's firewall, and forward them on the router if players come from the internet.
4. Give the other players the server's address, for example `192.0.2.10` or `192.0.2.10:36100`.

Players on the same home network can use the host PC's local address.

## 2. Every player sets up the launcher

1. Open the launcher and go to the **Online** tab.
2. Choose **re-psn**.
3. **Server:** the address from step 1. With no port given, 36100 is used.
4. **Online name:** 3 to 16 letters, digits, `-` or `_`, starting with a letter. Other players see this name. Each player
   on a server needs a different name.
5. Press **Save online settings**, or just **Start Game**, which saves first.

To turn online off again, choose **Off**.

## 3. Play

1. Main Menu > **Battle Modes** > **Online Battle** > **Friendly Match** (with the two-page menu off, `MENU_PAGES=0`: Main Menu >
   **Online Battle**). Online fights are 2 vs 2; 3 vs 3 is offline Free Battle only.
2. One player picks **Create a Room**; the others pick **Search for a Room** and join it.
3. When everyone is in the room, start the battle.

## Playing on the same network (LAN)

On a home network you can skip typing the host's address.

1. **The host** starts re-psn with `-discover` (and `-name "Some name"` if you want a name shown, at most 24 letters or
   digits). Add `-relay` as in step 1 above if needed. Discovery is off unless `-discover` is given.
2. **The others** open the launcher's **Online** tab, choose **re-psn** and press **Find on LAN**. The search takes about
   a second. One server found: its address goes into the Server field. Several: pick one from the list, which shows
   each one's name, address, players, relay on or off and version.
3. Press **Save online settings** and play as in step 3 above.

Limits:
- Same subnet only. It does not cross routers, VPNs or the internet; use the host's address for those.
- Wi-Fi networks with client isolation (guest networks, some public or hotel Wi-Fi) block it.
- A Windows firewall prompt may appear the first time; allow the launcher on private networks. The host's firewall must
  allow UDP port 36100 as well as the TCP port.
- If nothing is found, the Server field can still be filled by hand.
- Discovery needs re-psn 0.5.0 or later. Older releases do not answer and Find on LAN finds nothing.
- Find on LAN shows whatever answers on the local network. A reply is not authenticated, so pick the server you know.
- The launcher sends one small broadcast and shows only what answers; it does not connect to anything until you start
  the game.

## Rollback over re-psn (experimental)

By default the game's own online and the port's experimental rollback netcode run together: `repsn.ini` says `enabled=1`
and `play_settings.txt` has `JSP_ROLLBACK=1` with no role key (plus `JSP_ROLLBACK_RELAY`). The players meet in a Friendly
Match room as usual; once the room has both players, each game opens a second, hidden re-psn connection as
`<online name>-rb`, and the two meet in a rollback room keyed by the Friendly Match room's id (the room owner hosts). Rollback
then runs the fight, directly or through the server's relay when the direct link fails and the relay is allowed. US 1.00 disc
only; on another disc Online Battle plays on the game's own netcode. The settings window's Online tab (**Rollback netcode
for Online Battle fights**) and Options > Port Settings > Online (**Online Battle + rollback**) turn it on and off; see
[TESTING_ONLINE.md](TESTING_ONLINE.md).

The older rollback starts with a role key (`JSP_ROLLBACK_VIA=repsn` with `JSP_ROLLBACK_ROLE`, or direct IP) are a separate
method for tests: with a role key the game's own online must be off (`enabled = 0` in `repsn.ini`, which keeps the server and
online name), or the start is refused. The recipe is in
[ROLLBACK.md, "Set up rollback over re-psn"](ROLLBACK.md#set-up-rollback-over-re-psn).

## If it does not work

- **Online Battle says: You have been signed out of "PSN". You cannot play online battles.** Online is off: no settings were found, or they were refused (the log says which). Check that the Online tab shows re-psn and
  that the server address is right. The game's log lists its online decisions on lines that start with `[online]`.
- **Both methods on:** if re-psn and a community server are both enabled (by a hand edit), the game refuses online
  play and says so in the log (`[online] ERROR: re-psn and community are both enabled`). Pick one in the Online tab.
  An address given by hand (see below) is not affected.
- **The room is found but the battle does not start, or freezes:** the direct link between two players failed. Ask the
  host to start the server with `-relay`, or open UDP for the game on both PCs. The game's default peer port is 3658,
  and two copies on one PC need different ports (see below).
- **The top of the main menu says "Rollback is off: repsn.ini has enabled=1 (the game's own online)."** Rollback and the
  game's own online were both on, so neither runs and nothing can connect. For rollback, set `enabled = 0` in `repsn.ini`
  (keep `server` and `name`); for the game's own online, remove `JSP_ROLLBACK=1` from `play_settings.txt` (or set it to 0).
  The other "Rollback is off" texts are listed in [ROLLBACK.md](ROLLBACK.md#if-rollback-does-not-start).
- **Mods:** while the mods folder's list (`mods.txt`) holds a mod that changes gameplay, the game's own Online Battle is off
  for that session (the log says `[mods] game online refused: gameplay mod set active`, and the Online page says
  "gameplay mods are on"; turn the mods off to play it). Mods that only change the look or the sound (logos, music) do not turn it off.
  Rollback is meant to work when both players run the same mods, but that has not yet been run in a game (the mods folder is in
  progress, and there is no mod to install yet). Rollback is off when a listed gameplay mod could not be applied, or when a copy of the
  game's data that a gameplay mod changes differs from the one it was checked against (`[mods] set: refused` in the log).
- **Name refused:** use 3 to 16 letters, digits, `-` or `_`, starting with a letter, and not a name another player
  already uses on that server.
- **Rollback (experimental) over re-psn ends with `re-psn: no host with this version found within 2 minutes` or
  `re-psn: no player with this version joined within 2 minutes`:** the two games did not meet. Each game waits 2 minutes
  after its own start-up (the joining game keeps looking for the host's room), so start both at about the same time,
  the host first if you can. Both players need the same version and the same gameplay mods (or none): the message and the
  main menu show the build number (8 hex digits) to compare. See
  [ROLLBACK.md, Rollback over re-psn](ROLLBACK.md#rollback-over-re-psn-experimental).

## Where the settings live

The launcher writes plain text files, which you can also edit by hand while the game is closed:
- `repsn.ini` and `community.ini` in `%APPDATA%\PS3Recomp\`, or in `<install folder>\shared\` for a portable install;
- the environment variable `PS3RECOMP_SHARED_DIR` picks another folder.

```ini
[repsn]
enabled = 1
server = 192.0.2.10:36100
name = yourname
```

The game reads these files when it starts, so changes apply on the next start. Each decision is written to the game's
log on a line that starts with `[online]`. The game's own `repsn.ini` can also set `p2p_port`, in the game folder:
`<install folder>` for a portable install, else `%APPDATA%\JSP Fan Revival\`:

```ini
[repsn]
p2p_port = 3659
```

What wins, strongest first: the command-line options `--psnr` and `--username`, then the environment variables
(`PSNR_SERVER` for the server, `PS3_NP_ONLINE_ID` for the name, `PS3_NET_P2P_PORT` for the peer port, also when set in the
launcher's settings file), then the files. Setting `PS3_NET_ONLINE` alone changes nothing here: the server address
decides. An address given by hand also skips the "both methods on" refusal.

The peer port is 3658 unless `p2p_port` or `PS3_NET_P2P_PORT` says otherwise.

A community server, set up through the Community server presets, runs only the base game's online features. Features
that exist only in this port do not work there. This build has no client for a community server yet, so choosing one
keeps the game offline (the log says so).

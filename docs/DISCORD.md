# Discord Rich Presence

ENHANCEMENT, on by default. Your Discord profile shows what you are doing in the game, and the game's own online rooms get
Discord's Join, Ask to Join and Invite buttons. The game talks only to the Discord app on your own PC (its local pipe, the way every
game's Rich Presence works); it sends nothing anywhere else. Without the Discord app running, nothing happens.

## Turn it off or change it

`play_settings.txt` (in the install folder, or `%APPDATA%\JSP Fan Revival\`):

```
JSP_DISCORD=0
```

turns it off. There is no default application: Rich Presence stays off (the log says `[discord] Rich Presence off: no Discord application set`)
until `JSP_DISCORD_APP_ID` names one. To make one, create it (Discord Developer Portal, Applications, New Application; the name you
give it is the game name your profile shows), upload the pictures below to it and set `JSP_DISCORD_APP_ID=<its application id>`.
The log (`game\run\play_stderr.txt`) says `[discord] Rich Presence on` and `[discord] connected to the Discord app`.

## What it shows

| Where | Shown (first line / second line) |
|---|---|
| Menus | Title screen, Main menu, Training menu, Loading; Character select and Stage select with the mode under them (Free Battle, Training, J-Adventure) |
| Online | Online Battle menu; Hosting a Friendly Match / Waiting for an opponent (1 of 2); In a Friendly Match room / with the other player's online name (2 of 2); Online character select; After the fight |
| A fight | both teams from the fight itself, red first, with each team's Support when it fits Discord's 128 characters: "Luffy & Naruto (+Rukia) vs Goku & Ichigo (+Neuro)". Under it the mode (Free Battle, Training, J-Adventure, Offline battle for another menu entry, or "Online Friendly Match vs &lt;online name&gt; (rollback)"), then the KO score ("2-1 KOs", red first) when the port is counting KOs (online rollback, a KO count other than 3), then the stage when the game knows it for sure |
| Timer | in a fight, the time since that fight started (it starts again for every fight); elsewhere, the time since the game started |
| Big picture | key `logo` on every screen, fights included, with the text "J-Stars Victory VS EX" |
| Small picture | none by default. `JSP_DISCORD_CHAR_ART=1` (play_settings.txt) shows the player's fighter as key `char_NNN`, offline only |

The fighters come from the fight's own battle units (the battle world the game builds the fight from), in every mode: offline,
Training and the online fight. The menus' pick records are not used for the fight: the online fight never fills them (they read as
Luffy four times there). An id with no known name is left out; when a team has no known fighter
the first line is just "In battle". Character ids: fighters 0 to 41 (the game offers 39 of them; 19, 20 and 21 are not on its list),
Supports 200 to 213 (Neuro 200, Kumagawa, Rukia, Chitoge, Hisoka, Edajima, Kagura & Sadaharu, Jaguar, Yoh, Kuroko, SKET Dance,
Lala, Allen, Hinata 213; [Support.md](balance/Support.md)).

The log (`game\run\play_stderr.txt`) has a line each time the activity changes, `[discord] activity: details="..." state="..."
large_image="logo" large_text="..." small_image="..."`, with the other player's online name written as `<opponent>`, and once per
fight the fight's units, `[discord] fight units: <id>/t<team>/r<role>` (role 1 leader, 2 partner, 3 Support).

### Picture keys

The pictures are uploaded by whoever owns the Discord application (Discord Developer Portal, the application, Rich Presence, Art
Assets), with exactly these names, in lower case:

| Key | Used | Picture |
|---|---|---|
| `logo` | always, as the big picture | the VS EX logo, 1024x1024 recommended |
| `char_000` .. `char_041` | only with `JSP_DISCORD_CHAR_ART=1` | one portrait per fighter, by character id, the numbers of [character-details](character-details/) (`char_000` Luffy, `char_028` Gintoki) |

A key with no picture shows none; nothing else changes. Discord can take some minutes to show a newly uploaded picture. No picture
is part of this repository. The game name in "Playing ..." on the profile is the Discord application's name, set in the Developer
Portal, not in the game; the pictures must be uploaded to the application whose id the game uses (`JSP_DISCORD_APP_ID`).

## Join and Invite

Join works with the game's own Online Battle menus over re-psn (Friendly Match rooms). The online state comes from the runtime's
room emulation (toolkit patch 7924), read only.

- **Host.** While the room has space, the presence carries a party (the room, 1 of 2) and a join secret
  `jsp3|<re-psn server>|<room id>`. It holds no IP address. Discord then offers Join and Ask to Join on the host's profile, and
  Invite in a chat's "+" menu. Both games show the same party once the friend is in (2 of 2), and the secret is dropped. An Ask to
  Join request goes to Discord's own prompt, which asks the host. `JSP_DISCORD_AUTO_ACCEPT=1` accepts it automatically instead
  while the room has space (a full room always goes to the prompt).
- **Joiner, game running.** No restart. The game checks that the host's re-psn server is its own, then points its room search at
  that one room: Battle Modes > Online Battle > Friendly Match > Search lists only the friend's room until the join is done (at most
  3 minutes).
  The online-menu autopilot takes those steps by itself; without it, open the search and pick the one room. The autopilot gives
  the controller back at once when you press anything, after 60 seconds in any case, when a Yes/No box is in its way, or when the
  join is dropped, and it never joins a room from an unfiltered list.
- **Joiner, game not started.** Discord starts it through `discord-<id>://`. The game registers itself for the current user
  (`HKCU\Software\Classes`); `JSP_DISCORD_REGISTER=0` skips that. Discord delivers the join once the game is connected, and it
  goes the same way. `JSP_DISCORD_JOIN=<secret>` in the game's environment does the same at start.
- **When a join is refused.** The log line `[discord] join: ...` gives the reason: no re-psn server or online name here, the host
  is on another server, the join secret is not one this build reads, already in that room, or (from the search) the room is gone,
  full, or not a Friendly Match room.

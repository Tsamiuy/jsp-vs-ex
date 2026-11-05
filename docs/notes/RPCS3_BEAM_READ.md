# Reading one beam particle in RPCS3 (for the missing skill effects)

What this answers: in our port, two values in each beam particle's record stay zero (a size value, and a "draw"
flag). We need to see what they hold in RPCS3, where the beam works. You don't change anything in RPCS3, you only
look at memory and take screenshots.

## Steps

1. Start J-Stars in RPCS3 as usual, and get Goku's Kamehameha on screen. A fight where you fire it yourself is
   easiest; the attract demo also works if Goku fires it there.
2. While the beam is on screen, pause the game: in RPCS3, Emulation > Pause (or press Ctrl+P). The beam should
   stay frozen on screen.
3. Open the memory viewer: Debug > Memory Viewer (in some versions it's under Utilities, or in the Debugger window).
4. Type the address `49245840` into its address box and press Enter. That is where the first beam particle record
   sits in our port; RPCS3 may put it in the same place.
5. Check that you're on a particle record: 8 rows of 16 bytes below the start (offset +0x80) should read
   `43000000 43000000 43000000` followed by one more value (that's the colour 128, 128, 128). If it does, go to
   step 7.
6. If it doesn't match: use the memory viewer's search (if your version has one) to search for the hex bytes
   `43000000 43000000 43000000`. A record starts 0x80 bytes before each match. Records are 0x130 bytes apart, so
   the next one is at +0x130 from the start. If your version has no search, just take the screenshot in step 7 at
   `49245840` anyway, plus one at `4920FEE0`; I can often still tell from those.
7. Take a screenshot of the memory viewer showing the record from its start, and scroll so the screenshot covers
   the first 0x130 bytes (19 rows of 16 bytes). If one screenshot isn't enough, take two: one from the start, one
   from start + 0x100.
8. If you can, also take the same screenshots for the next record (start + 0x130).
9. Save the screenshots into `jstars-notes\reference\` (for example `rpcs3_beam_record_1.png`), then tell me.

## What I'll look at

- Start + 0x110 to + 0x117: in our port this reads `00000000 3F800000` (0, then 1.0). If RPCS3 shows a non-zero
  first value, our port is missing whatever sets it.
- Start + 0x120: in our port the second byte is `00`. If RPCS3 has `02` there (e.g. `0002....`), that's the
  missing "draw" flag.
- Start + 0x60 and + 0x100: I'll compare these as well; they're part of the same screenshots.

## If it's easier

A screenshot of the RPCS3 window showing the beam at the moment you paused also helps, so I know the frame matches.
RPCS3's save states use their own packed format, so I can't read a save state file directly; the memory viewer
screenshots are the simplest route.

## Update: finding the record in RPCS3 (the ?? at 49245840)

`49245840` is where the record sits in our port, and our port puts the game's big memory block at a different place
than a real PS3 or RPCS3 does. `??` means "nothing mapped here", which is also normal at address 0.

1. **Where to look in RPCS3.** In our port, the game's first big memory request (0xB800000 bytes) lands at
   0x40000000. On a PS3 (and in RPCS3) the same request normally lands at 0x30000000. So the same record should be
   at **`39245840`** in RPCS3. Try that first, and also `3920FEE0`. Check for the colour pattern 0x80 bytes in, as in
   step 5.
2. **Searching.** If neither works, search for the record's colour block, which is long enough to be fairly unique:
   `43 00 00 00 43 00 00 00 43 00 00 00 42 C8 00 00` (128, 128, 128, 100). RPCS3 stores memory as the PS3 does
   (big-endian), so type the bytes in that order. Use the search mode for raw bytes / hex. If there's no hex mode,
   try the 32-bit integer mode with `1124073472` (that's 0x43000000), though that finds many matches. I'm not sure
   which mode names your version shows, so try the hex/bytes one first, with and without spaces. A record starts
   0x80 bytes before a match. If the full 16-byte pattern finds nothing, the beam's alpha may differ from 100: search
   only `43 00 00 00 43 00 00 00 43 00 00 00` instead.
3. **Pausing.** The normal pause (Emulation > Pause) should be enough for reading memory. If the viewer still shows
   `??` at `39245840`, try pausing from the Debugger window instead (Debug > Debugger, then its pause button) and
   re-open the memory viewer.
4. **If nothing works.** A screenshot of the memory viewer at `30000000` and at `39240000` tells me whether that area
   is mapped at all in RPCS3, so I can work out the right base. I can't read RPCS3 save states offline, so the
   memory viewer is still the route.

## Update 2: search patterns, in order

1. **Test the layout:** go to `3903B270`. Ours (0x4903B270) holds `41F00000 3D088889` at +4 (30.0, 1/30). If RPCS3
   shows the same, the heaps line up and the record is just elsewhere.
2. **Test the search tool:** search `41F000003D088889`. It must find at least that spot; if it finds nothing, the
   search tool isn't usable on this memory, and we only use direct addresses.
3. **Find the beam's emitter:** search `3F8000003F6CCCCD3F4CCCCD3F800000` (the beam tint 1.0, 0.925, 0.8, 1.0, unique
   enough). Emitter block start = match - 0x70. Ours was 0x4908BDE0 (try `3908BDE0` directly too).
4. **From the emitter to the records:** search the emitter's start address as 8 hex digits (e.g. `3908BDE0`). One hit
   is the beam's frame block, laid out as `<emitter> <param block> <record> <output>`. The third value is a record
   address: screenshot 0x130 bytes there.
Longer patterns are safer; 16 bytes is plenty. Avoid patterns of only 0 / 1.0, they're everywhere.

## Update 3: no search, no known fixed chain yet

RPCS3's search doesn't respond, so we'd need a chain of pointers starting from a fixed address. I don't have one
yet: the particle system's root object is passed between game functions rather than kept at one fixed address I
could find by reading. One short run of our port (logging only) can find it: at beam time, scan the game's fixed
data area (0x00870000-0x00980000) for pointers that lead, in up to 3 steps, to the beam's particle list. That gives
a numbered recipe (address → read 4 bytes → next address → ... → record) for RPCS3. This needs a go-ahead for one
The AI assistant run. Until then, a manual alternative is to scroll the memory viewer from 39240000 to 39270000 looking for
rows `43000000 43000000 43000000`, but that's slow.

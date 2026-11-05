CPU BOOST SWITCH  (Maximum processor state 100 % = boost, 99 % = no boost)

WARNING (read first)
  This helper changes the "Maximum processor state" of the ACTIVE Windows power plan, for both the
  plugged-in (AC) and the battery (DC) value, with powercfg. It does not save the old value and it does
  not restore it. boost_on.bat sets 100 % and boost_off.bat sets 99 %, whatever the value was before.
  After boost_off.bat the plan stays at 99 %. The same happens when jstars\play.ps1 or jstars\run_test.ps1
  call these files (run_test.ps1 skips them when JSTARS_NO_BOOST=1 is set).
  Before the first use, write down your current value (see "First test", step 3). To go back, set that
  value by hand in Power Options. The helper is optional and not needed to build or run the port.
  Not tested on Windows by the author of this change; it is a text change only.

Written 2026-10-25. NOT tested on your PC yet: please try it and tell me what happens.

Files in this folder
  boost_toggle.bat   flips between 100 % and 99 % and shows a small pop-up message
  boost_on.bat       sets 100 % (boost allowed)
  boost_off.bat      sets 99 % (boost off)
  boost_status.bat   only shows the current state
  boost.ps1          the script the .bat files call (changes the AC "plugged in" and DC "battery"
                     values of the ACTIVE power plan, then re-activates it)
  boost_hotkey.ahk   optional AutoHotkey version of the hotkey (see option B)

Admin needed?  Normally NO. Changing the current plan's values with powercfg works for a normal user.
Some PCs from the maker lock the plan; then the pop-up says "The change did not apply".

First test (nothing hotkey-related yet)
  1. Double-click boost_status.bat. A message should say whether boost is ON or OFF.
  2. Double-click boost_toggle.bat, then boost_status.bat again. The state should have flipped.
  3. To double-check by hand: Control Panel > Power Options > Change plan settings > Change
     advanced power settings > Processor power management > Maximum processor state.
  (Task Manager's CPU speed shows if the clock stays under the boost speed when it is OFF.)

HOTKEY
  Fn cannot be bound: it is handled by the laptop's firmware, so Windows never sees it.
  Use Ctrl+Alt+B instead (you can pick another letter).

  Option A: no extra software (Windows shortcut key)
    1. Right-click boost_toggle.bat > Show more options > Send to > Desktop (create shortcut).
    2. Right-click the new shortcut on the desktop > Properties > Shortcut tab.
    3. Click "Shortcut key", press Ctrl+B (Windows turns it into Ctrl+Alt+B). Run: Minimized. OK.
    Windows only honours shortcut keys of shortcuts on the Desktop or in the Start menu, so
    keep the shortcut there. It can fail to fire while some full-screen games have focus.

  Option B: AutoHotkey v2 (works in more places, including games in windowed mode)
    1. Install AutoHotkey v2 from autohotkey.com (not installed by me; I did not check if you have it).
    2. Double-click boost_hotkey.ahk. Ctrl+Alt+B then toggles.
    3. To start it with Windows: press Win+R, type shell:startup, and put a shortcut to
       boost_hotkey.ahk there.

Notes
  - 99 % works on most Intel and AMD laptops under Windows because the CPU stops going above its
    base speed. On a few machines the plan's "Processor performance boost mode" setting is what
    counts; tell me if the speed still reaches 5 GHz with boost OFF and I will add that setting.
  - This changes the power plan of Windows (outside this project). To undo: run boost_on.bat.
  - Nothing here was run on your machine by me.

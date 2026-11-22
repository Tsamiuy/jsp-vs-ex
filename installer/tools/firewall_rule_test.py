#!/usr/bin/env python3
"""Checks the Windows Firewall rule ownership of installer/jsp.iss (the "firewall" task). Runs anywhere (plain Python, no Windows needed).

Setup adds one inbound rule named FirewallRule for this installation's game\\jsp.exe; setup (before adding it again) and uninstall must
remove only that rule: the exact owned name AND this installation's program path. Rules the player made, rules Windows made for the same
program, and the same-named rule of another installation in another folder must survive. This test holds the source rules and a model
of OwnFirewallRuleExists (the uninstall's search of netsh's verbose listing), checked on listings shaped like netsh's output.

Not run here: netsh itself. The manual check against a scratch rule set is in installer/README.md ("Firewall rule").

    python firewall_rule_test.py [REPO_ROOT]
"""
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[2])
iss = (root / "installer/jsp.iss").read_text(encoding="utf-8", errors="replace")
fails = []


def check(name, ok):
    print(("ok   " if ok else "FAIL ") + name)
    if not ok:
        fails.append(name)


def body(text, head):
    """The text of a [Code] function or procedure, from its head to its 'end;' at the start of a line."""
    i = text.index(head)
    return text[i:text.index("\nend;", i)]


code = iss[iss.index("[Code]"):] if "[Code]" in iss else iss
check("the owned rule name is defined once", re.search(r'^#define FirewallRule AppDirName \+ " online play"\s*$', iss, re.M) is not None)
check("no delete by name=all (that removes every rule of the program)", "name=all" not in iss.lower())
deletes = re.findall(r"delete rule[^\r\n]*", iss)
check("every delete names the owned rule and a program", deletes != [] and all('name="{#FirewallRule}"' in d and 'program="' in d for d in deletes))
check("the delete command is built in one place", len(deletes) == 1 and "function FirewallDeleteParams(Exe: String): String;" in iss)
check("no rule name typed out by hand (all through FirewallRule)", '"{#AppDirName} online play"' not in iss)
add = body(code, "procedure AddFirewallRule;")
check("setup: the old owned rule goes first, then the add, both for this installation's exe",
      "FirewallDeleteParams(Exe)" in add and add.index("FirewallDeleteParams(Exe)") < add.index("add rule")
      and 'name="{#FirewallRule}"' in add and 'program="\' + Exe + \'"' in add and "Exe := FirewallExe;" in add)
check("the exe is this installation's game\\jsp.exe", "Result := ExpandConstant('{app}\\game\\jsp.exe');" in body(code, "function FirewallExe: String;"))
un = body(code, "procedure CurUninstallStepChanged(")
check("uninstall: deletes only after finding this installation's rule, with the same owned delete",
      "OwnFirewallRuleExists(FirewallExe)" in un and "FirewallDeleteParams(FirewallExe)" in un
      and un.index("OwnFirewallRuleExists(FirewallExe)") < un.index("FirewallDeleteParams(FirewallExe)")
      and "show rule" not in un and "delete rule" not in un)
own = body(code, "function OwnFirewallRuleExists(Exe: String): Boolean;")
check("uninstall search: verbose listing of the owned name only, unreadable = no rule",
      'show rule name="{#FirewallRule}" verbose' in own and "Result := False;" in own and own.count("then Exit;") >= 2)
check("uninstall search: whole path at the end of a line, case-insensitive",
      "Pos(' ' + Uppercase(Exe) + #10, Listing) > 0" in own and "Uppercase(String(Raw))" in own and "StringChangeEx(Listing, #13#10, #10, True);" in own)


def own_rule_exists(listing, exe):
    """Model of OwnFirewallRuleExists: Uppercase, CRLF -> LF, a trailing line end added, then ' ' + EXE + LF."""
    s = (listing.upper() + "\r\n").replace("\r\n", "\n")
    return (" " + exe.upper() + "\n") in s


def rule(name, program, action="Allow"):
    return ("\r\nRule Name:                            %s\r\n" % name + "-" * 70 + "\r\nEnabled:                              Yes\r\n"
            "Direction:                            In\r\nProfiles:                             Private,Public\r\nGrouping:                             \r\n"
            "LocalIP:                              Any\r\nRemoteIP:                             Any\r\nProtocol:                             UDP\r\n"
            "Edge traversal:                       No\r\nProgram:                              %s\r\nInterfaceTypes:                       Any\r\n"
            "Security:                             NotRequired\r\nRule source:                          Local Setting\r\n"
            "Action:                               %s\r\n" % (program, action))


NAME = "JSP Fan Revival online play"
MINE = r"C:\Program Files\JSP Fan Revival\game\jsp.exe"
OTHER = r"D:\Games\JSP Fan Revival\game\jsp.exe"
check("model: this installation's rule is found", own_rule_exists(rule(NAME, MINE) + "Ok.\r\n", MINE))
check("model: found among other installations' rules", own_rule_exists(rule(NAME, OTHER) + rule(NAME, MINE) + "Ok.\r\n", MINE))
check("model: another installation's rule alone is not ours", not own_rule_exists(rule(NAME, OTHER) + "Ok.\r\n", MINE))
check("model: case differences in the path still match", own_rule_exists(rule(NAME, MINE.lower()), MINE))
check("model: a longer path that starts with ours is not ours", not own_rule_exists(rule(NAME, MINE + ".old"), MINE))
check("model: a path that ends with ours is not ours", not own_rule_exists(rule(NAME, "E:\\x" + MINE[2:]), MINE))
check("model: the last line without a line end still matches", own_rule_exists(("Program:                              " + MINE), MINE))
check("model: no rules", not own_rule_exists("No rules match the specified criteria.\r\n", MINE))

print("RESULT:", "ok" if not fails else "FAILED")
sys.exit(1 if fails else 0)

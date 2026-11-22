"""Evaluate the installer's Pascal firewall arguments without executing Windows commands."""
from pathlib import Path
import re
import shlex
import sys

INSTALLER = Path(__file__).resolve().parents[1] / "jsp.iss"
APP_PATH = r"C:\Games With Spaces\Fan Revival"


def pascal_string(expr, exe, app_name):
    parts = []
    for term in expr.strip().split("+"):
        term = term.strip()
        if term == "Exe":
            parts.append(exe)
        elif match := re.fullmatch(r"ExpandConstant\('([^']+)'\)", term):
            parts.append(match[1].replace("{app}", APP_PATH))
        elif match := re.fullmatch(r"'([^']*)'", term):
            parts.append(match[1].replace("{#AppDirName}", app_name))
        else:
            raise ValueError(f"Unrecognized Pascal term {term}")
    return "".join(parts)


def rule_args(text, operation):
    tokens = shlex.split(text)
    command = ["advfirewall", "firewall", operation, "rule"]
    start = tokens.index("advfirewall")
    assert tokens[start:start + len(command)] == command
    return dict(arg.split("=", 1) for arg in tokens[start + len(command):])


def check(iss):
    source = iss.read_text()
    app = re.search(r'^#define AppDirName "([^"]+)"', source, re.M)[1]
    install = source.split("procedure AddFirewallRule;", 1)[1].split("\nend;", 1)[0]
    path_expr = re.search(r"Exe := (.*?);", install)[1]
    exe = pascal_string(path_expr, "", app)
    assert exe == APP_PATH + r"\game\jsp.exe"
    params = pascal_string(re.search(r"Params := (.*?);", install, re.S)[1], exe, app)
    delete, add = params.split(" >nul 2>&1 & ")
    assert delete.startswith("/C netsh ") and add.startswith("netsh ")
    new = rule_args(add, "add")
    name = app + " online play"
    assert (new["name"], new["program"], new["dir"], new["action"]) == (name, exe, "in", "allow")
    uninstall = re.search(r"ShellExec\('runas', ExpandConstant\('\{sys\}\\netsh\.exe'\), (.*?), '', SW_HIDE", source)[1]
    for command in [delete, pascal_string(uninstall, "", app)]:
        selectors = rule_args(command, "delete")
        # Model netsh's conjunction of selectors, including its name=all wildcard.
        rules = [new, dict(new, name="My outbound block", dir="out", action="block"),
                 dict(new, program=r"D:\Second install\game\jsp.exe")]
        removed = [rule for rule in rules if all(rule.get(k) == v or (k, v) == ("name", "all")
                                                for k, v in selectors.items())]
        assert removed == [new], (command, removed)


if __name__ == "__main__":
    check(Path(sys.argv[1]) if len(sys.argv) == 2 else INSTALLER)

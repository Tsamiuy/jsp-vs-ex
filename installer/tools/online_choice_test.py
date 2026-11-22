#!/usr/bin/env python3
"""Compile real launchers; check the writer and source-level privacy/navigation guards."""
import pathlib
import shutil
import subprocess
import tempfile

TEST = r'''using System;
using System.Collections.Generic;
using System.Reflection;
class OnlineChoiceTests {
 static int Main() {
  MethodInfo apply = null;
  foreach (Type t in Assembly.GetExecutingAssembly().GetTypes()) {
   MethodInfo m = t.GetMethod("RbApply", BindingFlags.NonPublic | BindingFlags.Static);
   if (m != null) { if (apply != null) throw new Exception("ambiguous writer"); apply = m; }
  }
  if (apply == null) throw new Exception("actual writer missing");
  Environment.SetEnvironmentVariable("JSP_ROLLBACK", "1");
  int n = 0;
  foreach (string old in new string[] { null, "JSP_ROLLBACK=1", "jsp_rollback=1" }) {
   var lines = new List<string> { "# preserved", "UNRELATED=keep" };
   if (old != null) lines.Add(old);
   apply.Invoke(null, new object[] { lines, false, false, true, true, "36200", "127.0.0.1:36200", false, "" });
   int matches = 0;
   foreach (string line in lines) if (line.StartsWith("JSP_ROLLBACK=", StringComparison.OrdinalIgnoreCase)) {
    if (line != "JSP_ROLLBACK=0") throw new Exception("non-rollback selection did not turn rollback off"); matches++;
   }
   if (matches != 1 || !lines.Contains("# preserved") || !lines.Contains("UNRELATED=keep"))
    throw new Exception("missing explicit override or unrelated lines damaged");
   n++;
  }
  var native = apply.DeclaringType.GetMethod("RbNative", BindingFlags.NonPublic | BindingFlags.Static);
  var transition = new List<string> { "JSP_ROLLBACK=0", "JSP_ROLLBACK_VIA=repsn", "JSP_ROLLBACK_ROLE=auto", "JSP_ROLLBACK_CODE=ABCD", "JSP_ROLLBACK_BENCH=1", "UNRELATED=keep" };
  native.Invoke(null, new object[] { transition, true });
  if (transition.Count != 3 || !transition.Contains("JSP_ROLLBACK=1") || !transition.Contains("JSP_ROLLBACK_RELAY=1") || !transition.Contains("UNRELATED=keep"))
   throw new Exception("native selected with a stale custom role or code");
  apply.Invoke(null, new object[] { transition, true, false, true, false, "36200", "", false, "ABCD" });
  if (!transition.Contains("JSP_ROLLBACK_VIA=repsn") || !transition.Contains("JSP_ROLLBACK_ROLE=auto") || !transition.Contains("JSP_ROLLBACK_CODE=ABCD") || !transition.Contains("JSP_ROLLBACK_RELAY=0"))
   throw new Exception("custom auto/code choice lost after native");
  n += 2;
  // Use the read-only instance method without constructing a form or touching a real settings folder.
  Type launcher = apply.DeclaringType;
  var instance = System.Runtime.Serialization.FormatterServices.GetUninitializedObject(launcher);
  GC.SuppressFinalize(instance);
  var reader = launcher.GetMethod("SettingsValue", BindingFlags.NonPublic | BindingFlags.Instance);
  var file = launcher.GetField("settingsFile", BindingFlags.NonPublic | BindingFlags.Instance);
  string path = System.IO.Path.GetTempFileName();
  try {
   file.SetValue(instance, path);
   foreach (bool relay in new bool[] { false, true }) {
    native.Invoke(null, new object[] { transition, relay });
    System.IO.File.WriteAllLines(path, transition.ToArray());
    string before = System.IO.File.ReadAllText(path);
    if ((string)reader.Invoke(instance, new object[] { "JSP_ROLLBACK_RELAY" }) != (relay ? "1" : "0"))
     throw new Exception("native relay preference did not survive reload");
    if (System.IO.File.ReadAllText(path) != before) throw new Exception("settings read changed the saved file");
    n++;
   }
  } finally { System.IO.File.Delete(path); }
  Console.WriteLine("online choice: " + n + " actual-writer cases passed"); return 0;
 }
}
'''


def main():
    root = pathlib.Path(__file__).resolve().parents[2]
    compiler, runtime = shutil.which("mcs"), shutil.which("mono")
    if not compiler or not runtime:
        raise SystemExit("mcs and mono are required for actual C# behavior tests")
    with tempfile.TemporaryDirectory(prefix="jsp-online-choice-") as scratch:
        d = pathlib.Path(scratch)
        test = d / "OnlineChoiceTests.cs"
        test.write_text(TEST, encoding="utf-8")
        for name in ("JStarsLauncher.cs", "JStarsLauncher19.cs"):
            source = (root / "launcher" / name).read_text(encoding="utf-8")
            # These guards cover startup/UI call sites without constructing a Windows Form.
            assert 'Log("Launcher start. Settings saved.");' in source, name
            assert 'string.Join(" | ", File.ReadAllLines(settingsFile))' not in source, name
            assert 'var p = onlineTab = new TabPage("Online")' in source, name
            assert 'tabs.SelectedTab = onlineTab;' in source, name
            assert 'Shot("online", tabs.TabPages.IndexOf(onlineTab), -1);' in source, name
            assert 'tabs.TabPages.Count - 1' not in source, name
            exe = d / (name + ".exe")
            subprocess.run([compiler, "-sdk:4.5", "-langversion:5", "-r:System.Windows.Forms",
                            "-r:System.Drawing", "-main:OnlineChoiceTests", "-out:" + str(exe),
                            str(root / "launcher" / name), str(test)], check=True)
            subprocess.run([runtime, str(exe)], check=True)
    print("online choice: both complete launcher variants passed")


if __name__ == "__main__":
    main()

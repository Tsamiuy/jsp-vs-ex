#!/usr/bin/env python3
"""Compile and exercise the actual ModsManager in each launcher; no GUI or game."""
import pathlib
import shutil
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
TEST = r'''using System;
using System.IO;
using System.Text;
using System.Collections.Generic;
namespace JStarsLauncher {
class ModsManagerTests {
 static int checks;
 static void Check(bool c, string why) { checks++; if (!c) throw new Exception(why); }
 static void Refuses(Action a, string why) { bool failed=false; try { a(); } catch(Exception) { failed=true; } Check(failed, why); }
 static string Order(ModsManager m) { var ids=new List<string>(); foreach(var e in m.Entries) ids.Add((e.Enabled?"":"-")+e.Id); return string.Join(",",ids.ToArray()); }
 static void Mod(string root,string id) { Directory.CreateDirectory(Path.Combine(root,id)); File.WriteAllText(Path.Combine(root,id,"mod.json"),"{}"); }
 static void Main() {
  string root=Path.Combine(Path.GetTempPath(),"jsp-mod-manager-"+Guid.NewGuid().ToString("N")); Directory.CreateDirectory(root);
  try {
   string mods=Path.Combine(root,"mods"), settings=Path.Combine(root,"play_settings.txt"), list=Path.Combine(root,"mods.txt");
   Mod(mods,"zeta"); Mod(mods,"alpha");
   Func<string,string> empty=delegate(string k){return null;};
   var m=ModsManager.Load(root,settings,root,false,empty);
   Check(Order(m)=="alpha,zeta","folder discovery order"); Check(!File.Exists(list),"opening manager does not save");
   File.WriteAllText(list,"# heading\r\n-zeta # keep this comment\r\nghost\r\nzeta # duplicate comment\r\n",new UTF8Encoding(true));
   m=ModsManager.Load(root,settings,root,false,empty);
   Check(Order(m)=="-zeta,ghost,alpha","first entry wins; missing directive retained"); Check(!m.Entries[1].Installed,"missing folder visible");
   m.Entries[0].Enabled=true; m.Move(2,0); m.Save();
   string saved=File.ReadAllText(list); m.Save(); Check(File.ReadAllText(list)==saved,"repeated save is byte-identical"); Check(saved.Contains("# heading")&&saved.Contains("# keep this comment")&&saved.Contains("# duplicate comment"),"comments preserved");
   Check(Order(ModsManager.Load(root,settings,root,false,empty))=="alpha,zeta,ghost","saved order and missing intent");
   Check(File.ReadAllBytes(list)[0]==239,"BOM preserved");
   File.AppendAllText(list,"# concurrent edit\n"); Refuses(delegate{m.Save();},"external change rejected"); Check(File.ReadAllText(list).EndsWith("# concurrent edit\n"),"external bytes preserved");
   File.WriteAllText(list,"bad id\n"); Refuses(delegate{ModsManager.Load(root,settings,root,false,empty);},"invalid list read-only"); Check(File.ReadAllText(list)=="bad id\n","invalid list unchanged");
   File.WriteAllText(list,new string('x',1048577)); Refuses(delegate{ModsManager.Load(root,settings,root,false,empty);},"oversized list refused");
   File.WriteAllBytes(list,new byte[]{255,254,253}); Refuses(delegate{ModsManager.Load(root,settings,root,false,empty);},"invalid UTF8 refused");
   File.Delete(list); Directory.CreateDirectory(list); Refuses(delegate{ModsManager.Load(root,settings,root,false,empty);},"directory is not a missing list"); Directory.Delete(list);
   File.WriteAllText(settings,"# preferences stay here\nJSP_MODS_DIR=chosen\nJSP_MODS_LIST=choices.txt\nJSP_MODS=0\n");
   string originalSettings=File.ReadAllText(settings); Mod(Path.Combine(root,"chosen"),"alpha");
   Func<string,string> env=delegate(string k){return "inherited";};
   m=ModsManager.Load(root,settings,root,false,env); Check(m.DirectoryPath==Path.Combine(root,"chosen")&&m.ListPath==Path.Combine(root,"choices.txt"),"settings beat inherited env; relative paths use game cwd");
   m.Entries[0].Enabled=false; m.Save(); Check(File.ReadAllText(settings)==originalSettings&&m.GlobalOff,"preferences and global kill switch preserved and visible");
   Check(ModsManager.QuoteArgument("")=="\"\""&&ModsManager.QuoteArgument("plain")=="\"plain\"","quoted empty and plain args");
   Check(ModsManager.QuoteArgument("a\"b")=="\"a\\\"b\""&&ModsManager.QuoteArgument("tail\\")=="\"tail\\\\\"","quotes and trailing backslashes");
   string args=m.PackageArguments(root,Path.Combine(root,"spaces & name.zip")); Check(args.Contains(" --archive \"")&&args.Contains("spaces & name.zip\"")&&args.Contains(" --mods \""),"package paths are quoted arguments");
   Refuses(delegate{m.InstallZip(root,Path.Combine(root,"any.zip"));},"missing bundled tools refuse without a system Python fallback");
   var child=new System.Collections.Specialized.StringDictionary(); m.Export(child); Check(child["JSP_MODS_DIR"]==m.DirectoryPath&&child["JSP_MODS_LIST"]==m.ListPath,"legacy launch gets manager paths");
   File.WriteAllText(Path.Combine(root,"paths.txt"),"JSP_MODS_DIR=chosen\nJSP_MODS_LIST=from-paths.txt\n");
   File.WriteAllText(settings,"JSP_MODS_LIST=last.txt\n"); m=ModsManager.Load(root,settings,root,true,env); Check(m.DirectoryPath==Path.Combine(root,"chosen")&&m.ListPath==Path.Combine(root,"last.txt"),"paths.txt then settings precedence");
   m.Save(); Check(Directory.GetFiles(root,"*.tmp-*").Length==0,"no temporary file remains");
   File.WriteAllText(settings,"jsp_mods_list=@APP@/mixed#name.txt\n"); m=ModsManager.Load(root,settings,root,true,env); Check(m.ListPath==Path.Combine(root,"mixed#name.txt"),"installed mixedcase key, app expansion, hash filename");
   File.WriteAllText(settings," JSP_MODS_LIST=ignored.txt\nJSP_MODS_LIST= leading.txt \n"); m=ModsManager.Load(root,settings,root,true,env); Check(m.ListPath==Path.Combine(root," leading.txt"),"installed keeps leading value space, ignores padded key, trims trailing space");
   File.WriteAllText(settings,"jsp_mods_list=legacy.txt # comment\nJSP_MODS_DIR=chosen\n"); m=ModsManager.Load(root,settings,root,false,env); Check(m.ListPath==Path.Combine(root,"legacy.txt"),"legacy inline comments and mixedcase keys");
   File.WriteAllText(settings,"JSP_MODS_LIST=changed.txt\\n"); Refuses(delegate{m.Save();},"changed path preferences rejected");
   Console.WriteLine("Mods manager: "+checks+" behavior checks passed");
  } finally { Directory.Delete(root,true); }
 }
}
}'''


def main():
    compiler = shutil.which("mcs")
    mono = shutil.which("mono")
    if not compiler or not mono:
        raise SystemExit("mcs and mono are required (CI provisions them)")
    sources = [ROOT / "launcher" / name for name in ("JStarsLauncher.cs", "JStarsLauncher19.cs")]
    models = [p.read_text().split("// BEGIN MODS MANAGER", 1)[1].split("// END MODS MANAGER", 1)[0] for p in sources]
    if models[0] != models[1]:
        raise SystemExit("Mods manager logic differs between launcher variants")
    with tempfile.TemporaryDirectory(prefix="jsp-mod-manager-test-") as tmp:
        test = pathlib.Path(tmp) / "test.cs"
        test.write_text(TEST)
        for source in sources:
            exe = pathlib.Path(tmp) / (source.stem + ".exe")
            subprocess.run([compiler, "-sdk:4.5", "-langversion:5", "-r:System.Windows.Forms", "-r:System.Drawing", "-main:JStarsLauncher.ModsManagerTests", "-out:" + str(exe), str(source), str(test)], check=True)
            subprocess.run([mono, str(exe)], check=True)
        model = pathlib.Path(tmp) / "model.cs"
        model.write_text("using System; using System.IO; using System.Text; using System.Collections.Generic; using System.Diagnostics; using System.Threading; namespace JStarsLauncher {\n" + models[0].split("\n", 1)[1] + "\n}")
        exe = pathlib.Path(tmp) / "model-net40.exe"
        subprocess.run([compiler, "-sdk:4", "-langversion:5", "-out:" + str(exe), str(model), str(test)], check=True)
        subprocess.run([mono, str(exe)], check=True)
    print("Both complete launcher sources compile; actual Mods manager behavior agrees")


if __name__ == "__main__":
    main()

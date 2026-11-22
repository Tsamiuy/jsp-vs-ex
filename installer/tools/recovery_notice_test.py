#!/usr/bin/env python3
"""Compile both standalone launchers and test exact bounded recovery records (requires mcs/mono)."""
import os,pathlib,subprocess,tempfile
ROOT=pathlib.Path(__file__).resolve().parents[2]
HARNESS=r'''using System; using System.Text; using System.IO; using System.Reflection; using System.Threading;
namespace JStarsLauncher { class RecoveryHarness {
static void Check(bool b) { if(!b) throw new Exception("recovery assertion"); }
static int Send(RecoveryNotice p,string s) { int n=0;foreach(byte b in Encoding.ASCII.GetBytes(s)) if(p.Feed(b)!=0)n++;return n; }
static int Main() { object owner=new object(); var p=new RecoveryNotice(owner);
Check(Send(p,"[step] FAILED old log\nJSP_SESSION/1 STOPPED 1 CLOCK\n")==0);
Check(Send(p,"JSP_SESSION/1 START 1\n")==0); Check(p.CanHide(owner));
Check(Send(p,"JSP_SESSION/1 STOPPED 1 CLOCK")==0); Check(p.CanHide(owner)); Check(p.Feed(10)==1);
Check(!p.CanHide(owner)&&p.CanShow(owner,1)&&!p.CanShow(new object(),1));
Check(Send(p,"JSP_SESSION/1 STOPPED 1 CLOCK\nJSP_SESSION/1 START 1\n")==0); Check(!p.CanHide(owner));
Send(p,"JSP_SESSION/1 START 2\n");Check(!p.CanShow(owner,1)&&p.CanHide(owner));
Check(Send(p,"JSP_SESSION/1 STOPPED 1 CLOCK\nJSP_SESSION/1 STOPPED 2 WRONG\nJSP_SESSION/1 STOPPED 2 CLOCK extra\n")==0);
Send(p,"JSP_SESSION/1 RETIRE 2\n");Check(Send(p,"JSP_SESSION/1 STOPPED 2 CLOCK\n")==0);
Check(Send(p,"JSP_SESSION/1 START 0\nJSP_SESSION/1 START 03\nJSP_SESSION/1 START 18446744073709551616\n")==0);
Send(p,new string('x',200)+"JSP_SESSION/1 START 3\n");Check(Send(p,"JSP_SESSION/1 STOPPED 3 CLOCK\n")==0);
Send(p,"JSP_SESSION/1 START 3\n");Check(Send(p,"JSP_SESSION/1 STOPPED 3 CLOCK\n")==1);
Send(p,"JSP_SESSION/1 START 4\r\n");Check(Send(p,"JSP_SESSION/1 STOPPED 4 CLOCK\r\n")==1);
Send(p,"JSP_SESSION/1 START 5\rX\n");Check(Send(p,"JSP_SESSION/1 STOPPED 5 CLOCK\n")==0);
// Actual pipe-reader helper must drain even when file creation fails; no Form/display required.
int count=0; var input=new MemoryStream(Encoding.ASCII.GetBytes("plain log\n"));
var pump=typeof(LauncherForm).GetMethod("Pump",BindingFlags.NonPublic|BindingFlags.Static);
var thread=(Thread)pump.Invoke(null,new object[]{input,Path.Combine(Path.GetTempPath(),Guid.NewGuid().ToString(),"absent","log.txt"),new Action<byte>(delegate(byte b){count++;})});
Check(thread.Join(2000)&&count==10);Console.WriteLine("recovery parser/lifecycle/process/hide/fragment/oversize/failed-sink PASS"); return 0;
} } }'''
def main():
 with tempfile.TemporaryDirectory() as td:
  td=pathlib.Path(td);h=td/'test.cs';h.write_text(HARNESS)
  for name in ['JStarsLauncher.cs','JStarsLauncher19.cs']:
   exe=td/(name+'.exe')
   subprocess.run([os.environ.get('MCS','mcs'),'-sdk:4.5','-langversion:5','-r:System.Windows.Forms','-r:System.Drawing','-main:JStarsLauncher.RecoveryHarness','-out:'+str(exe),str(ROOT/'launcher'/name),str(h)],check=True)
   subprocess.run([os.environ.get('MONO','mono'),str(exe)],check=True)
if __name__=='__main__':main()

// Read-only Linux attachment diagnostic. No Roblox/Windows offsets or remote writes.
#define _GNU_SOURCE 1
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <elf.h>
#include "../daemon/platform/linux_elf.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#include <dlfcn.h>
#if __has_include(<X11/Xlib.h>)
#include <X11/Xlib.h>
#define PICK_X11 1
#endif
bool imagesOnly=false;
struct Region { unsigned long start,end,offset; std::string perms,path; };
std::string text(const std::string& p) { std::ifstream f(p); return {std::istreambuf_iterator<char>(f),{}}; }
std::string linkto(const std::string& p) { char b[4096]; auto n=readlink(p.c_str(),b,sizeof(b)); return n>0?std::string(b,n):""; }
std::string clean(std::string s) { for(char& c:s) if((unsigned char)c<32 || c==127)c=' '; return s; }
std::vector<Region> maps(unsigned pid,int& error) {
 std::vector<Region> out; errno=0; std::ifstream f("/proc/"+std::to_string(pid)+"/maps"); error=f?0:errno;
 std::string line; while(std::getline(f,line)) { Region r{}; char perm[5]; int n=0;
 if(sscanf(line.c_str(),"%lx-%lx %4s %lx %*s %*s %n",&r.start,&r.end,perm,&r.offset,&n)!=4)continue;
 r.perms=perm; r.path=line.substr(n); out.push_back(r); } return out;
}
bool module(const Region& r) {
 // Match module basenames, not command lines. Include deleted mappings.
 auto p=r.path; if(p.size()>10 && p.substr(p.size()-10)==" (deleted)")p.resize(p.size()-10);
 auto pos=p.find_last_of('/'); auto base=p.substr(pos==std::string::npos?0:pos+1);
 return base=="libroblox.so" || base=="libbadcpu.so";
}
// NSpid is ordered from the reader's namespace to successively nested ones.
bool windowPidMatches(const std::string& status, unsigned advertised) {
 std::istringstream lines(status); std::string line;
 while(std::getline(lines,line))if(line.rfind("NSpid:",0)==0){
 std::istringstream values(line.substr(6));unsigned n,last=0;while(values>>n)last=n;
 return last!=0 && last==advertised;
 }
 return false;
}
std::vector<unsigned> windowCandidates(unsigned advertised) {
 std::vector<unsigned> out;DIR* d=opendir("/proc");if(!d)return out;dirent* e;
 while((e=readdir(d))){char* end;auto p=strtoul(e->d_name,&end,10);if(*end || !p)continue;
 auto root="/proc/"+std::to_string(p);struct stat st{};
 if(stat(root.c_str(),&st) || st.st_uid!=getuid())continue;
 if(!windowPidMatches(text(root+"/status"),advertised))continue;
 int error;auto rs=maps(p,error);bool hit=false;for(auto& r:rs)hit|=module(r);
 if(hit)out.push_back(static_cast<unsigned>(p));
 }
 closedir(d);std::sort(out.begin(),out.end());return out;
}
unsigned pick() {
#ifdef PICK_X11
 void* lib=dlopen("libX11.so.6",RTLD_NOW|RTLD_LOCAL); if(!lib){std::cerr<<"X11 library unavailable\n";return 0;}
#define FN(name) auto name##_=reinterpret_cast<decltype(&name)>(dlsym(lib,#name)); if(!name##_)return 0;
 FN(XOpenDisplay) FN(XDefaultRootWindow) FN(XGrabPointer) FN(XAllowEvents) FN(XNextEvent) FN(XUngrabPointer) FN(XInternAtom) FN(XGetWindowProperty) FN(XQueryTree) FN(XFree) FN(XCloseDisplay)
 Display* d=XOpenDisplay_(nullptr); if(!d){std::cerr<<"No X11 display (native Wayland selection is not supported)\n";return 0;}
 Window root=XDefaultRootWindow_(d); unsigned pid=0;
 if(XGrabPointer_(d,root,False,ButtonPressMask|ButtonReleaseMask,GrabModeSync,GrabModeAsync,None,None,CurrentTime)!=GrabSuccess){XCloseDisplay_(d);return 0;}
 std::cerr<<"Click the Sober client window.\n"; XEvent ev{};
 do { XAllowEvents_(d,SyncPointer,CurrentTime); XNextEvent_(d,&ev); }while(ev.type!=ButtonPress);
 Window selected=ev.xbutton.subwindow?ev.xbutton.subwindow:root;
 XUngrabPointer_(d,CurrentTime);
 Atom prop=XInternAtom_(d,"_NET_WM_PID",True);
 // Search decoration/client subtree. Bounded traversal.
 std::vector<Window> pending{selected}; unsigned visited=0;
 while(!pending.empty() && visited++<4096 && !pid) {
 Window w=pending.back();pending.pop_back(); Atom actual;int format;unsigned long count,after;unsigned char* data=nullptr;
 if(prop && XGetWindowProperty_(d,w,prop,0,1,False,AnyPropertyType,&actual,&format,&count,&after,&data)==Success) {
 if(data && format==32 && count==1)pid=static_cast<unsigned>(*reinterpret_cast<unsigned long*>(data));
 if(data)XFree_(data);
 }
 Window rr,parent,*children=nullptr;unsigned n=0;
 if(!pid && XQueryTree_(d,w,&rr,&parent,&children,&n)){for(unsigned i=0;i<n;i++)pending.push_back(children[i]); if(children)XFree_(children);}
 }
 XCloseDisplay_(d);dlclose(lib);return pid;
#else
 std::cerr<<"Built without X11 headers; use automatic discovery or --pid.\n";return 0;
#endif
}
bool inspect(unsigned pid,bool explicitTarget) {
 std::string root="/proc/"+std::to_string(pid); auto before=text(root+"/stat");
 int error=0;auto regions=maps(pid,error); bool found=false;for(auto& r:regions)found|=module(r);
 if(!found && !explicitTarget)return false;
 std::cout<<"\nTARGET host_pid="<<pid<<" comm="<<clean(text(root+"/comm"))<<" exe="<<clean(linkto(root+"/exe"))<<"\n";
 std::istringstream status(text(root+"/status"));std::string line;while(std::getline(status,line)) if(line.rfind("NSpid:",0)==0 || line.rfind("Uid:",0)==0)std::cout<<line<<"\n";
 std::cout<<"pid_namespace="<<linkto(root+"/ns/pid")<<" maps_count="<<regions.size()<<" maps_errno="<<error<<" ("<<strerror(error)<<")\n";
 // Compact file/memfd inventory: one record per path, never thousands of lines
 // for repeated mappings. Paths identify backing objects, not game semantics.
 struct Summary {size_t count=0;unsigned long bytes=0;bool exec=false;};
 std::map<std::string,Summary> inventory;size_t anonymous=0;unsigned long anonBytes=0;
 for(auto& r:regions){if(r.path.empty() || r.path[0]=='['){anonymous++;anonBytes+=r.end-r.start;continue;}
 auto& item=inventory[r.path];item.count++;item.bytes+=r.end-r.start;item.exec|=r.perms.find('x')!=std::string::npos;}
 for(auto& item:inventory)if(!imagesOnly || item.first.rfind("/memfd:",0)==0)std::cout<<"BACKING mappings="<<item.second.count<<" bytes="<<item.second.bytes<<" executable="<<item.second.exec<<" path="<<clean(item.first)<<"\n";
 std::cout<<"ANONYMOUS_OR_SPECIAL mappings="<<anonymous<<" bytes="<<anonBytes<<"\n";
 bool readOK=false; unsigned modules=0;
 for(auto& r:regions) {
 bool recognized=module(r);
 if(imagesOnly && !recognized && r.path.rfind("/memfd:",0)!=0)continue;
 // Inspect ELF headers of other offset-zero file/memfd mappings as well.
 // This surfaces anonymous-name loaders without claiming which ELF is Roblox.
 if(!recognized && (r.offset!=0 || r.path.empty() || r.path[0]!='/'))continue;
 if(recognized)modules++;
 std::cout<<(recognized?"MAP start=0x":"OTHER_ELF_PROBE start=0x")<<std::hex<<r.start<<" end=0x"<<r.end<<" file_offset=0x"<<r.offset<<std::dec<<" perms="<<r.perms<<" path="<<clean(r.path)<<"\n";
 if(r.offset!=0 || r.perms.empty() || r.perms[0]!='r')continue;
 Elf64_Ehdr header{};iovec local{&header,sizeof(header)},remote{reinterpret_cast<void*>(r.start),sizeof(header)};errno=0;
 auto n=process_vm_readv(pid,&local,1,&remote,1,0);int e=n<0?errno:0;
 std::cout<<"READ requested="<<sizeof(header)<<" got="<<n<<" errno="<<e<<" ("<<strerror(e)<<")\n";
 if(n!=sizeof(header) || memcmp(header.e_ident,ELFMAG,SELFMAG))continue;
 std::cout<<"ELF class="<<unsigned(header.e_ident[EI_CLASS])<<" endian="<<unsigned(header.e_ident[EI_DATA])<<" machine="<<header.e_machine<<"\n";
 auto image=linux_elf::Inspect(pid,r.start);
 if(!image.valid)continue;
 std::cout<<"ELF_LOAD_BIAS=0x"<<std::hex<<image.bias<<std::dec<<"\n";
 if(recognized)readOK=true;
 std::cout<<"ELF_SONAME="<<(image.soname.empty()?"UNAVAILABLE":image.soname)<<"\n";
 if(image.soname=="libroblox.so")std::cout<<"IMAGE_IDENTITY=SONAME_LIBROBLOX (ELF self-identification; game-object layout still unvalidated)\n";
 if(!image.build_id.empty())std::cout<<"GNU_BUILD_ID="<<image.build_id<<"\n";
 }

 auto identity=[](const std::string& stat){auto p=stat.rfind(')');std::istringstream f(p==std::string::npos?"":stat.substr(p+1));std::string v;for(int field=3;field<=22;field++)if(!(f>>v))return std::string{};return v;};
 auto start=identity(before);bool stable=!start.empty() && start==identity(text(root+"/stat"));
 std::cout<<"PROCESS_START_TICKS="<<start<<" identity_stable="<<stable<<" module_mappings="<<modules<<"\n";
 std::cout<<"ATTACH_READ_ONLY="<<(readOK&&stable?"PASS":"FAIL")<<" OFFSET_RESOLUTION=NOT_ATTEMPTED\n";
 return readOK&&stable;
}
int main(int argc,char** argv){
 unsigned pid=0;bool explicitTarget=false,windowTarget=false;
 if(argc==2 && std::string(argv[1])=="--images"){imagesOnly=true;}
 else if(argc==2 && std::string(argv[1])=="--pick"){pid=pick();windowTarget=true;}
 else if(argc==3 && (std::string(argv[1])=="--pid" || std::string(argv[1])=="--window-pid")){char* end;unsigned long n=strtoul(argv[2],&end,10);if(*end || !n || n>2147483647){std::cerr<<"PID must be a positive numeric value, not the literal HOST_PID placeholder.\n";return 2;}pid=n;windowTarget=std::string(argv[1])=="--window-pid";explicitTarget=!windowTarget;}
 else if(argc!=1){std::cerr<<"Usage: sober-attach [--pid HOST_PID | --pick | --window-pid ADVERTISED_PID | --images]\n";return 2;}
 std::cout<<"SOBER_ATTACH_RECEIPT_V3 read_only=1\nyama="<<clean(text("/proc/sys/kernel/yama/ptrace_scope"))<<"\n";
 if(windowTarget){
 if(!pid){std::cerr<<"No window PID obtained.\n";return 2;}
 auto targets=windowCandidates(pid);
 std::cout<<"WINDOW_ADVERTISED_PID="<<pid<<" namespace_matching_candidates="<<targets.size()<<"\n";
 // Different PID namespaces can reuse the same number. Never rank ties or
 // interpret the window's integer as an unrelated host PID.
 if(targets.size()!=1){for(auto p:targets)std::cout<<"AMBIGUOUS_HOST_PID="<<p<<" namespace="<<linkto("/proc/"+std::to_string(p)+"/ns/pid")<<"\n";
 std::cerr<<"Window PID not uniquely resolved among same-user recognized-module processes; use automatic inventory or explicit --pid.\n";return 2;}
 std::cout<<"WINDOW_RESOLVED_HOST_PID="<<targets.front()<<" (NSpid match plus module evidence; not game-object validation)\n";
 return inspect(targets.front(),true)?0:1;
 }
 if(explicitTarget)return pid?(inspect(pid,true)?0:1):2;
 DIR* d=opendir("/proc");if(!d)return 2;unsigned candidates=0,denied=0,passed=0;dirent* e;
 while((e=readdir(d))){char* end;auto p=strtoul(e->d_name,&end,10);if(*end || !p)continue;std::string root="/proc/"+std::to_string(p);struct stat st{};if(stat(root.c_str(),&st) || st.st_uid!=getuid())continue;
 int error;auto rs=maps(p,error);if(error==EACCES || error==EPERM)denied++;bool hit=false;for(auto& r:rs)hit|=module(r);
 if(hit){candidates++;passed+=inspect(p,false);}else{auto comm=text(root+"/comm");if(comm=="sober\n" || comm=="Sober\n")std::cout<<"UNVERIFIED_CANDIDATE pid="<<p<<" comm="<<clean(comm)<<" maps_errno="<<error<<" (no recognized module)\n";}}
 closedir(d);std::cout<<"SUMMARY candidates="<<candidates<<" readable_targets="<<passed<<" same_uid_maps_denied="<<denied<<"\n";
 if(!candidates)std::cout<<"No recognized game module observed. Use --pick or --pid to inspect a target without module-based discovery. No namespace or permission cause is assumed.\n";
 return passed?0:1;
}

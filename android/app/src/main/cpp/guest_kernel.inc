// Included inside the portable runtime namespace: all pointers use ptr(),
// every return follows the 32-bit stdcall stack contract.
uint32_t guestCommandLine{}, guestEnvironmentA{}, guestEnvironmentW{}, exceptionFilter{};
uint32_t processErrorMode{};
void setErrorMode(){uint32_t old=processErrorMode;processErrorMode=arg(0);ret(old,1);}
std::array<bool,1088> tlsUsed{};
thread_local std::array<uint32_t,1088> tlsValues{};
std::map<uint32_t,std::shared_ptr<std::recursive_mutex>> criticalSections;
std::map<uint32_t,bool> pendingCriticalDeletes;
uint32_t ansiString(const std::string& value) {
    auto address=allocate(static_cast<uint32_t>(value.size()+1));
    std::memcpy(ptr(address,value.size()+1),value.c_str(),value.size()+1); return address;
}
std::vector<uint16_t> wideString(uint32_t address) {
    std::vector<uint16_t> result;
    for(uint32_t i=0;i<32768;++i) {
        uint16_t c; std::memcpy(&c,ptr(address+i*2,2),2);
        if(!c) return result;
        result.push_back(c);
    }
    throw GuestStop("Unterminated UTF16 string");
}
uint32_t utf16String(const std::string& ascii) {
    uint32_t out=allocate(static_cast<uint32_t>((ascii.size()+1)*2));
    for(uint32_t i=0;i<ascii.size();++i) { uint16_t c=static_cast<unsigned char>(ascii[i]); std::memcpy(ptr(out+i*2,2),&c,2); }
    return out;
}
void getLastError() { ret(lastError,0); }
void setLastError() { lastError=arg(0); if(g_fs_base) write32(g_fs_base+0x34,lastError); ret(0,1); }
void tlsAlloc() {
    for(uint32_t i=0;i<tlsUsed.size();++i) if(!tlsUsed[i]) {tlsUsed[i]=true; tlsValues[i]=0; ret(i,0); return;}
    lastError=259; ret(UINT32_MAX,0);
}
void tlsFree() { uint32_t i=arg(0); bool ok=i<tlsUsed.size() && tlsUsed[i]; if(ok){tlsUsed[i]=false; tlsValues[i]=0;} else lastError=87; ret(ok,1); }
void tlsGet() {uint32_t i=arg(0); if(i>=tlsUsed.size()||!tlsUsed[i]){lastError=87;ret(0,1);}else{lastError=0;ret(tlsValues[i],1);}}
void tlsSet() {uint32_t i=arg(0); if(i>=tlsUsed.size()||!tlsUsed[i]){lastError=87;ret(0,2);}else{tlsValues[i]=arg(1);ret(1,2);}}
void initCritical(uint32_t address,uint32_t spin) {
    if(criticalSections.count(address)&&read32(address+8))throw GuestStop("Reinitializing active critical section");
    pendingCriticalDeletes.erase(address);
    criticalSections[address]=std::make_shared<std::recursive_mutex>();
    std::memset(ptr(address,24),0,24); write32(address+4,UINT32_MAX); write32(address+20,spin);
}
void initializeCritical() {initCritical(arg(0),0);ret(0,1);}
void initializeCriticalSpin() {initCritical(arg(0),arg(1));ret(1,2);}
void enterCritical() {
    uint32_t address=arg(0); auto cs=criticalSections.find(address);
    if(cs==criticalSections.end()){
        // A CRITICAL_SECTION copied by value after InitializeCriticalSection keeps
        // its state in the struct, and Windows accepts it: LockCount -1 (unlocked),
        // no recursion, no owner. Adopt exactly that; anything else is a real bug.
        if(read32(address+4)==UINT32_MAX&&read32(address+8)==0&&read32(address+12)==0){
            static unsigned adopted=0;
            if(++adopted<=8)NFS_RUNTIME_LOG(ANDROID_LOG_WARN,"NFSMW","Adopting copied critical section %08x from %08x",address,g_cur_func);
            criticalSections[address]=std::make_shared<std::recursive_mutex>();
            cs=criticalSections.find(address);
        }else{
            std::ostringstream dump;for(unsigned i=0;i<24;i+=4)dump<<' '<<hex(read32(address+i));
            throw GuestStop("EnterCriticalSection without initialization: "+hex(address)+" from "+hex(g_cur_func)+" words"+dump.str());
        }
    }
    auto mutex=cs->second;
    // Contended: hand the machine over (the fair lock gives it to the waiting
    // owner) instead of sleeping 1 ms per attempt; sleep only if it persists.
    if(!mutex->try_lock()){
        WaitScope scope("EnterCriticalSection",address);
        unsigned spins=0;
        while(!mutex->try_lock()){
            if(spins<64){++spins;checkGuestProgress();yieldGuestMachine(0);continue;}
            // Then sleep until LeaveCriticalSection signals it (event-driven waits).
            blockGuestUntil([&]{return mutex->try_lock();},noDeadline,{mutex.get()});break;   // holds the mutex once ready
        }
    }
    cs=criticalSections.find(address);
    if(cs==criticalSections.end()||cs->second!=mutex){mutex->unlock();throw GuestStop("Critical section deleted while waiting");}
    auto count=read32(address+8)+1; write32(address+8,count); write32(address+4,count-1); write32(address+12,guestThreadId);
    if(count==1)currentTrace().hold(address);
    ret(0,1);
}
void leaveCritical() {
    uint32_t address=arg(0); auto cs=criticalSections.find(address); uint32_t count=read32(address+8);
    if(cs==criticalSections.end()||!count||read32(address+12)!=guestThreadId) throw GuestStop("Unbalanced LeaveCriticalSection");
    write32(address+8,--count);write32(address+4,count-1);if(!count){write32(address+12,0);currentTrace().release(address);}cs->second->unlock();
    if(!count){
        signalGuestSync(cs->second.get());
        if(pendingCriticalDeletes.erase(address))criticalSections.erase(cs);
    }
    ret(0,1);
}
void deleteCritical() {
    uint32_t address=arg(0);
    if(read32(address+8)){
        NFS_RUNTIME_LOG(ANDROID_LOG_WARN,"NFSMW","Deferring deletion of active guest critical section %08x owner=%u count=%u",address,read32(address+12),read32(address+8));
        pendingCriticalDeletes[address]=true;
    }else{criticalSections.erase(address);pendingCriticalDeletes.erase(address);}
    ret(0,1);
}
void startupInfo() {std::memset(ptr(arg(0),68),0,68);write32(arg(0),68);ret(0,1);}
void setHandleCount() {ret(arg(0),1);}
void commandLine() {if(!guestCommandLine)guestCommandLine=ansiString("\"C:\\nfsmw\\speed.exe\"");ret(guestCommandLine,0);}
void moduleHandleW() {
    if(!arg(0)){ret(imageBase,1);return;}
    auto chars=wideString(arg(0));std::string name;
    for(auto c:chars){if(c>127){lastError=126;ret(0,1);return;}name.push_back(static_cast<char>(c));}
    name=lower(name);if(name=="speed2.exe"){ret(imageBase,1);return;}
    if(name.find('.')==std::string::npos)name+=".dll";
    auto found=modules.find(name);if(found==modules.end())lastError=126;ret(found==modules.end()?0:found->second,1);
}
std::map<uint32_t,uint32_t> moduleReferences;
void loadLibrary(){
    auto name=lower(guestString(arg(0)));auto slash=name.find_last_of("/\\");if(slash!=std::string::npos)name=name.substr(slash+1);if(name.find('.')==std::string::npos)name+=".dll";
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSMW","Guest LoadLibrary %s",name.c_str());
    auto module=modules.find(name);
    if(module==modules.end()){uint32_t loaded=loadGuestModule(name);if(!loaded)lastError=126;ret(loaded,1);return;}
    auto& count=moduleReferences[module->second];if(!count)count=1;++count;ret(module->second,1);
}
void freeLibrary(){auto module=std::find_if(modules.begin(),modules.end(),[](const auto& item){return item.second==arg(0);});
    if(module==modules.end()){if(freeGuestModule(arg(0))){ret(1,1);return;}lastError=6;ret(0,1);return;}
    auto found=moduleReferences.find(arg(0));if(found!=moduleReferences.end()&&found->second>1)--found->second;ret(1,1);}
void environmentA() {
    if(!guestEnvironmentA) {std::string s="PATH=C:\\nfsmw";s.push_back(0);s+="TEMP=C:\\nfsmw";s.push_back(0);s.push_back(0);guestEnvironmentA=ansiString(s);}
    ret(guestEnvironmentA,0);
}
void environmentW() {
    if(!guestEnvironmentW) {std::string s="PATH=C:\\nfsmw";s.push_back(0);s+="TEMP=C:\\nfsmw";s.push_back(0);s.push_back(0);guestEnvironmentW=utf16String(s);}
    ret(guestEnvironmentW,0);
}
void freeEnvironmentA() {bool ok=arg(0)==guestEnvironmentA && guestEnvironmentA;if(ok){heaps->free(GuestHeaps::process,guestEnvironmentA);guestEnvironmentA=0;}ret(ok,1);}
void freeEnvironmentW() {bool ok=arg(0)==guestEnvironmentW && guestEnvironmentW;if(ok){heaps->free(GuestHeaps::process,guestEnvironmentW);guestEnvironmentW=0;}ret(ok,1);}
void moduleFileName() {
    uint32_t module=arg(0),out=arg(1),capacity=arg(2);
    if(module && module!=imageBase) {lastError=126;ret(0,3);return;}
    const std::string path="C:\\nfsmw\\speed.exe";
    uint32_t count=std::min(capacity,static_cast<uint32_t>(path.size()));
    if(count)std::memcpy(ptr(out,count),path.data(),count);
    if(capacity>count)*static_cast<char*>(ptr(out+count,1))=0;else lastError=122;
    ret(count,3);
}
void currentDirectory() {const std::string path="C:\\nfsmw"; uint32_t cap=arg(0),out=arg(1);if(cap<=path.size())ret(static_cast<uint32_t>(path.size()+1),2);else{std::memcpy(ptr(out,path.size()+1),path.c_str(),path.size()+1);ret(static_cast<uint32_t>(path.size()),2);}}
void stdHandle() {uint32_t n=arg(0);ret(n>=0xfffffff4u&&n<=0xfffffff6u?0xf0000200u+(0xfffffff6u-n)*4:UINT32_MAX,1);}
void consoleMode() {lastError=6;ret(0,2);}
void currentProcess() {ret(UINT32_MAX,0);}
void currentThread() {ret(0xfffffffeu,0);}
void processId() {ret(static_cast<uint32_t>(getpid()),0);}
void threadId() {ret(guestThreadId,0);}
void version() {ret(5u|(1u<<8)|(2600u<<16),0);}
void isDebugger() {ret(0,0);}
void processorFeature() {
    // Match the virtual x86 CPUID supplied to lifted code, not the ARM host.
    uint32_t feature=arg(0);
    ret(feature==2||feature==3||feature==6||feature==8||feature==10,1);
}
void setExceptionFilter() {uint32_t old=exceptionFilter;exceptionFilter=arg(0);ret(old,1);}
void encodePointer() {ret(arg(0)^0x51c39a71u,1);}
void getACP() {ret(1252,0);}
void getOEMCP() {ret(437,0);}
void threadLocale() {ret(0x409,0);}
void cpInfo() {
    uint32_t cp=arg(0);if(cp!=0&&cp!=1252&&cp!=437&&cp!=65001){lastError=87;ret(0,2);return;}
    uint32_t out=arg(1);std::memset(ptr(out,20),0,20);write32(out,cp==65001?4:1);*static_cast<char*>(ptr(out+4,1))='?';ret(1,2);
}
uint64_t counter() {return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
void tickCount() {ret(static_cast<uint32_t>(counter()/1000000),0);}
void performanceCounter() {uint64_t now=counter();std::memcpy(ptr(arg(0),8),&now,8);ret(1,1);}
void performanceFrequency() {uint64_t frequency=1000000000;std::memcpy(ptr(arg(0),8),&frequency,8);ret(1,1);}
void systemFileTime() {uint64_t ticks=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count()/100)+116444736000000000ull;std::memcpy(ptr(arg(0),8),&ticks,8);ret(0,1);}
void fileTimeToLocalFileTime(){
    uint32_t input=arg(0),output=arg(1);
    if(input==output||!memory->readable(input,8)||!memory->writable(output,8)){lastError=87;ret(0,2);return;}
    // GetTimeZoneInformation exposes UTC for the virtual Windows machine.
    std::memcpy(ptr(output,8),ptr(input,8),8);lastError=0;ret(1,2);
}
void fileTimeToSystemTime(){
    uint32_t input=arg(0),output=arg(1);
    if(!memory->readable(input,8)||!memory->writable(output,16)){lastError=87;ret(0,2);return;}
    uint64_t ticks;std::memcpy(&ticks,ptr(input,8),8);
    if(ticks>=0x8000000000000000ull){lastError=87;ret(0,2);return;}
    int64_t seconds=static_cast<int64_t>(ticks/10000000ull)-11644473600ll;
    time_t unixTime=static_cast<time_t>(seconds);std::tm utc{};
    if(!gmtime_r(&unixTime,&utc)||utc.tm_year+1900>65535){lastError=87;ret(0,2);return;}
    uint16_t fields[]={uint16_t(utc.tm_year+1900),uint16_t(utc.tm_mon+1),uint16_t(utc.tm_wday),
        uint16_t(utc.tm_mday),uint16_t(utc.tm_hour),uint16_t(utc.tm_min),uint16_t(utc.tm_sec),
        uint16_t((ticks%10000000ull)/10000ull)};
    std::memcpy(ptr(output,16),fields,16);lastError=0;ret(1,2);
}
void systemInfo() {uint32_t out=arg(0);std::memset(ptr(out,36),0,36);write32(out+4,4096);write32(out+8,0x10000);write32(out+12,0x7ffeffff);write32(out+16,1);write32(out+20,1);write32(out+24,586);write32(out+28,65536);uint16_t level=6;std::memcpy(ptr(out+32,2),&level,2);ret(0,1);}
void sleepShim() {
    uint32_t ms=arg(0);if(ms==UINT32_MAX)throw GuestStop("Infinite Sleep requires guest scheduler");
    WaitScope scope("Sleep",ms);
    if(ms)blockGuestUntil([]{return false;},guestDeadline(ms));
    ret(0,1);
}
void sleepEx(){
    uint32_t ms=arg(0);if(!ms){checkGuestProgress();ret(0,2);return;}
    WaitScope scope("SleepEx",ms);
    blockGuestUntil([]{return false;},guestDeadline(ms));
    ret(0,2);
}
void exitProcess() {throw GuestStop("Guest requested ExitProcess("+std::to_string(arg(0))+") from "+hex(g_cur_func));}
void debugString() {NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSMW","Guest: %s",guestString(arg(0)).c_str());ret(0,1);}
void deleteGdiObject(){lastError=6;ret(0,1);} // No GDI-created objects are registered yet.
std::map<uint32_t,uint32_t> timerPeriods;
void timeCaps(){if(!arg(0)||arg(1)<8){ret(97,2);return;}write32(arg(0),1);write32(arg(0)+4,1000000);ret(0,2);}
void timeBegin(){uint32_t period=arg(0);if(!period||period>1000000){ret(97,1);return;}++timerPeriods[period];ret(0,1);}
void timeEnd(){auto period=timerPeriods.find(arg(0));if(period==timerPeriods.end()){ret(97,1);return;}if(!--period->second)timerPeriods.erase(period);ret(0,1);}
const std::map<std::string,recomp_func_t> timerFunctions={
    {"timeSetEvent",timeSetEvent},{"timeKillEvent",timeKillEvent},
    {"timeGetDevCaps",timeCaps},{"timeBeginPeriod",timeBegin},{"timeEndPeriod",timeEnd},{"timeGetTime",tickCount}
};
struct ModuleSnapshot {uint32_t flags;bool consumed=false,processConsumed=false;};
std::map<uint32_t,ModuleSnapshot> moduleSnapshots;
uint32_t nextSnapshot=0xf0010000;
std::map<uint32_t,uint32_t> kernelHandleAliases;
uint32_t nextKernelHandle=0xf0020000;
uint32_t guestPriorityClass=0x20;
bool processHandle(uint32_t handle){auto alias=kernelHandleAliases.find(handle);return handle==UINT32_MAX||(alias!=kernelHandleAliases.end()&&alias->second==UINT32_MAX);}
void getPriorityClass(){if(!processHandle(arg(0))){lastError=6;ret(0,1);return;}ret(guestPriorityClass,1);}
void isBadReadPtr(){ret(memory->readable(arg(0),arg(1))?0:1,2);}
void isBadWritePtr(){ret(memory->writable(arg(0),arg(1))?0:1,2);}
void virtualProtect(){
    uint32_t output=arg(3),old=0,flags=arg(2);
    if(!output||!memory->writable(output,4)){lastError=87;ret(0,4);return;}
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSMW","Guest VirtualProtect address=%08x bytes=%u protection=%x",arg(0),arg(1),flags);
    if(flags&~0xffu)throw GuestStop("VirtualProtect modifiers pending: "+hex(flags));
    if(!memory->protect(arg(0),arg(1),flags,old)){lastError=87;ret(0,4);return;}
    if(!memory->writable(output,4))throw GuestStop("VirtualProtect old-protection output became inaccessible");
    write32(output,old);ret(1,4);
}
void getTimeZoneInformation(){
    uint32_t output=arg(0);if(!output){lastError=87;ret(UINT32_MAX,1);return;}
    // The virtual Windows machine uses UTC with no daylight-saving transition.
    std::memset(ptr(output,172),0,172);
    const uint16_t name[]={u'U',u'T',u'C',0};
    std::memcpy(ptr(output+4,sizeof name),name,sizeof name);
    std::memcpy(ptr(output+88,sizeof name),name,sizeof name);ret(0,1);
}
void getProcessAffinityMask(){
    if(!processHandle(arg(0))){lastError=6;ret(0,3);return;}
    if(!arg(1)||!arg(2)){lastError=87;ret(0,3);return;}
    // The serialized guest machine exposes one virtual x86 processor.
    write32(arg(1),1);write32(arg(2),1);ret(1,3);
}
void setProcessAffinityMask(){
    if(!processHandle(arg(0))){lastError=6;ret(0,2);return;}
    if(arg(1)!=1){lastError=87;ret(0,2);return;}ret(1,2);
}
void setPriorityClass(){
    if(!processHandle(arg(0))){lastError=6;ret(0,2);return;}uint32_t priority=arg(1);
    if(priority!=0x20&&priority!=0x40&&priority!=0x80&&priority!=0x100&&priority!=0x4000&&priority!=0x8000){lastError=87;ret(0,2);return;}
    guestPriorityClass=priority;ret(1,2);
}
void getThreadPriority(){
    auto thread=threadHandles.find(arg(0));if(thread!=threadHandles.end()){ret(thread->second->priority,1);return;}
    auto alias=kernelHandleAliases.find(arg(0));if(arg(0)==UINT32_MAX-1||(alias!=kernelHandleAliases.end()&&alias->second==UINT32_MAX-1)){ret(0,1);return;}
    lastError=6;ret(0x7fffffff,1);
}
void duplicateHandle(){
    uint32_t source=arg(1),out=arg(3);
    if(arg(0)!=UINT32_MAX||arg(2)!=UINT32_MAX||!out){lastError=6;ret(0,7);return;}
    auto alias=kernelHandleAliases.find(source);uint32_t object=alias==kernelHandleAliases.end()?source:alias->second;
    if(object!=UINT32_MAX&&object!=UINT32_MAX-1){lastError=6;ret(0,7);return;}
    uint32_t handle=nextKernelHandle++;kernelHandleAliases.emplace(handle,object);write32(out,handle);
    if(arg(6)&1)kernelHandleAliases.erase(source);
    ret(1,7);
}
void toolhelpSnapshot(){
    uint32_t flags=arg(0),pid=arg(1);
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSMW","Toolhelp snapshot flags=%x pid=%u",flags,pid);
    if((flags&~0x8000001au)||!(flags&0x1a))throw GuestStop("Toolhelp snapshot flags pending: "+hex(flags));
    if(pid&&pid!=1){lastError=87;ret(UINT32_MAX,2);return;}
    uint32_t handle=nextSnapshot++;moduleSnapshots.emplace(handle,ModuleSnapshot{flags});ret(handle,2);
}
void moduleEntry(bool first){
    auto snapshot=moduleSnapshots.find(arg(0));
    if(snapshot==moduleSnapshots.end()){lastError=6;ret(0,2);return;}
    if(!(snapshot->second.flags&0x18)){lastError=18;ret(0,2);return;}
    if(first)snapshot->second.consumed=false;
    if(snapshot->second.consumed){lastError=18;ret(0,2);return;}
    uint32_t out=arg(1);if(read32(out)<548){lastError=24;ret(0,2);return;}
    std::memset(ptr(out,548),0,548);write32(out,548);write32(out+4,1);write32(out+8,1);
    write32(out+12,1);write32(out+16,1);write32(out+20,imageBase);write32(out+24,imageSize);write32(out+28,imageBase);
    const char name[]="speed.exe",path[]="C:\\nfsmw\\speed.exe";
    std::memcpy(ptr(out+32,sizeof name),name,sizeof name);std::memcpy(ptr(out+288,sizeof path),path,sizeof path);
    snapshot->second.consumed=true;ret(1,2);
}
void moduleFirst(){moduleEntry(true);}void moduleNext(){moduleEntry(false);}
void processEntry(bool first){
    auto snapshot=moduleSnapshots.find(arg(0));
    if(snapshot==moduleSnapshots.end()){lastError=6;ret(0,2);return;}
    if(first)snapshot->second.processConsumed=false;
    if(!(snapshot->second.flags&2)||snapshot->second.processConsumed){lastError=18;ret(0,2);return;}
    uint32_t out=arg(1);if(read32(out)<296){lastError=24;ret(0,2);return;}
    std::memset(ptr(out,296),0,296);write32(out,296);write32(out+8,1);write32(out+20,1);write32(out+28,8);
    const char name[]="speed.exe";std::memcpy(ptr(out+36,sizeof name),name,sizeof name);
    snapshot->second.processConsumed=true;ret(1,2);
}
void processFirst(){processEntry(true);}void processNext(){processEntry(false);}
void closeHandle(){if(closeGuestFile(arg(0))||moduleSnapshots.erase(arg(0))||kernelHandleAliases.erase(arg(0))||threadHandles.erase(arg(0))||eventHandles.erase(arg(0))||mutexHandles.erase(arg(0))){ret(1,1);return;}lastError=6;ret(0,1);}
struct RegistryValue {uint32_t type;std::vector<uint8_t> bytes;};
std::map<std::string,RegistryValue> gameRegistry;
// The game keeps its video options in the registry (g_CarLodLevel,
// g_RoadReflectionEnable, ...): Windows keeps them across launches, so they
// are stored in the game folder. The launcher owns the install path and the
// language; the render resolution and antialiasing are not restored, so a
// value that crashes a GPU cannot crash every following start.
std::string registryFile(){return hostGameRoot+"/android_registry.dat";}
bool registryPersisted(const std::string& name){
    return name!="install dir"&&name!="language"&&name!="g_racingresolution"&&name!="g_fsaalevel";
}
void loadPersistedRegistry(){
    std::ifstream file(registryFile(),std::ios::binary);if(!file)return;
    auto read=[&](void* data,size_t bytes){return bool(file.read(static_cast<char*>(data),std::streamsize(bytes)));};
    uint32_t count=0,restored=0;if(!read(&count,4)||count>4096)return;
    for(uint32_t i=0;i<count;++i){
        uint32_t nameBytes=0,type=0,size=0;
        if(!read(&nameBytes,4)||nameBytes==0||nameBytes>256)return;
        std::string name(nameBytes,'\0');if(!read(name.data(),nameBytes)||!read(&type,4)||!read(&size,4)||size>65536)return;
        RegistryValue value{type,std::vector<uint8_t>(size)};if(size&&!read(value.bytes.data(),size))return;
        if(registryPersisted(name)){gameRegistry[name]=std::move(value);++restored;}
    }
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFS","Registry: restored %u saved game settings",restored);
}
void savePersistedRegistry(){
    std::string target=registryFile(),temporary=target+".tmp";
    {std::ofstream file(temporary,std::ios::binary|std::ios::trunc);if(!file)return;
        uint32_t count=0;for(auto& [name,value]:gameRegistry)if(registryPersisted(name))++count;
        file.write(reinterpret_cast<const char*>(&count),4);
        for(auto& [name,value]:gameRegistry){if(!registryPersisted(name))continue;
            uint32_t nameBytes=uint32_t(name.size()),size=uint32_t(value.bytes.size());
            file.write(reinterpret_cast<const char*>(&nameBytes),4);file.write(name.data(),nameBytes);
            file.write(reinterpret_cast<const char*>(&value.type),4);file.write(reinterpret_cast<const char*>(&size),4);
            if(size)file.write(reinterpret_cast<const char*>(value.bytes.data()),size);}
        if(!file)return;}
    std::rename(temporary.c_str(),target.c_str());
}
std::map<uint32_t,bool> registryHandles;
uint32_t nextRegistryHandle=0xf0050000;
void registryOpen(uint32_t output,uint32_t argc){
    auto key=lower(guestString(arg(1)));
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSMW","Registry open %08x\\%s",arg(0),key.c_str());
    if(arg(0)!=0x80000002u||(key!="software\\ea games\\need for speed most wanted"&&key!="software\\electronic arts\\ea games\\need for speed most wanted\\ergc")){
        if(output)write32(output,0);ret(2,argc);return;
    }
    if(gameRegistry.empty()){
        const char path[]="C:\\nfsmw\\";gameRegistry.emplace("install dir",RegistryValue{1,{std::begin(path),std::end(path)}});
        auto language=guestLanguage;language.push_back('\0');gameRegistry.emplace("language",RegistryValue{1,{language.begin(),language.end()}});
        loadPersistedRegistry();
    }
    uint32_t handle=nextRegistryHandle++;registryHandles.emplace(handle,true);write32(output,handle);ret(0,argc);
}
void regOpenKeyA(){registryOpen(arg(2),3);}void regOpenKeyExA(){registryOpen(arg(4),5);}
void regCloseKey(){ret(registryHandles.erase(arg(0))?0:6,1);}
void regQueryValueExA(){
    auto name=lower(guestString(arg(1)));NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSMW","Registry query %08x value=%s",arg(0),name.c_str());
    if(!registryHandles.count(arg(0))){ret(6,6);return;}
    auto value=gameRegistry.find(name);if(value==gameRegistry.end()){ret(2,6);return;}
    if(!arg(5)){ret(87,6);return;}uint32_t size=read32(arg(5));write32(arg(5),value->second.bytes.size());
    if(arg(3))write32(arg(3),value->second.type);
    if(arg(4)){if(size<value->second.bytes.size()){ret(234,6);return;}std::memcpy(ptr(arg(4),value->second.bytes.size()),value->second.bytes.data(),value->second.bytes.size());}
    ret(0,6);
}
void regSetValueExA(){
    if(!registryHandles.count(arg(0))){ret(6,6);return;}
    uint32_t size=arg(5);if(size>1048576){ret(87,6);return;}
    RegistryValue value{arg(3),std::vector<uint8_t>(size)};if(size)std::memcpy(value.bytes.data(),ptr(arg(4),size),size);
    auto name=lower(guestString(arg(1)));gameRegistry[name]=std::move(value);
    if(registryPersisted(name))savePersistedRegistry();
    ret(0,6);
}
#include "guest_charset.inc"
#include "guest_resources.inc"
#include "guest_window.inc"
#include "guest_format.inc"
static constexpr uint32_t desktopWindow=0xf0000300;
void desktopHandle(){ret(desktopWindow,0);}
// Speed.exe destroys its window on the way out (0x006DFA80, after "Quit"); the
// Android surface stays with the activity, which closes with the process.
// Reports showed the quit stop as "HLE pendiente: user32.dll!DestroyWindow".
void destroyWindow(){ret(windows.count(arg(0))?1:0,1);}
void windowRect(){
    int32_t rectangle[4]={0,0,static_cast<int32_t>(displayWidth.load()),static_cast<int32_t>(displayHeight.load())};
    if(arg(0)!=desktopWindow){auto window=windows.find(arg(0));if(window==windows.end()){lastError=1400;ret(0,2);return;}auto& value=window->second;rectangle[0]=value.x;rectangle[1]=value.y;rectangle[2]=value.x+value.width;rectangle[3]=value.y+value.height;}
    std::memcpy(ptr(arg(1),16),rectangle,16);ret(1,2);
}
// Screen position of the touch pointer (the game maps it through GetWindowRect/GetClientRect).
void cursorPosition(){
    auto point=pointerInWindow();int32_t screen[2]={point.first,point.second};
    if(auto window=windows.find(activeWindow);window!=windows.end()){screen[0]+=window->second.x;screen[1]+=window->second.y;}
    std::memcpy(ptr(arg(0),8),screen,8);ret(1,1);
}
// Speed.exe clamps its mouse cursor (0x4131A0) to its window's client rect, in
// render pixels: report the back buffer it created (0x71A96C) for that window,
// since the Android surface, not the window, is what gets presented.
void clientRect(){auto window=windows.find(arg(0));if(window==windows.end()){lastError=1400;ret(0,2);return;}int32_t rect[4]={0,0,window->second.width,window->second.height};
    if(game::presentParams&&arg(0)==(game::windowHandle?read32(game::windowHandle):0u)){int32_t w=int32_t(read32(game::presentParams)),h=int32_t(read32(game::presentParams+4));if(w>0&&h>0&&w<=8192&&h<=8192){rect[2]=w;rect[3]=h;}}std::memcpy(ptr(arg(1),16),rect,16);ret(1,2);}
void loadIcon(){lastError=1813;ret(0,2);} // PE icon resources are not materialized as Android objects.
void loadCursor(){
    uint32_t id=arg(1);
    if(arg(0)==0&&id>=32512&&id<=32651){ret(0xf0000400+id,2);return;}
    lastError=1813;ret(0,2);
}
void systemMetrics(){
    uint32_t index=arg(0),value;
    switch(index){
    case 0:case 16:case 59:case 61:case 78:value=displayWidth.load();break;
    case 1:case 17:case 60:case 62:case 79:value=displayHeight.load();break;
    case 2:case 3:case 4:case 5:case 6:case 7:case 8:case 32:case 33:case 76:case 77:value=0;break;
    case 19:case 23:case 43:value=0;break; // No physical mouse declared by the Android host yet.
    case 80:value=1;break;
    default:throw GuestStop("GetSystemMetrics index pending: "+std::to_string(index));
    }
    ret(value,1);
}
// MessageBoxA(hwnd, text, caption, type): no dialog on Android. The text goes to
// the log (tester reports) and the box answers its default button.
void messageBox(){
    auto text=guestString(arg(1)),caption=guestString(arg(2));uint32_t type=arg(3)&0xf;
    NFS_RUNTIME_LOG(ANDROID_LOG_WARN,"NFSMW","Guest MessageBoxA from %08x: [%s] %s",g_cur_func,caption.c_str(),text.c_str());
    // MB_OK/OKCANCEL -> IDOK, ABORTRETRYIGNORE -> IDIGNORE, YESNO(CANCEL) -> IDYES,
    // RETRYCANCEL -> IDCANCEL, CANCELTRYCONTINUE -> IDCONTINUE.
    static constexpr uint32_t answers[]={1,1,5,6,6,2,11};
    ret(type<7?answers[type]:1,4);
}
// The pointer follows touch; a game that recentres the cursor gets success and no jump.
void setCursorPos(){ret(1,2);}
// CharUpperA: a pointer whose high word is zero carries one character in its low byte.
void charUpperA(){
    uint32_t value=arg(0);
    if(!(value>>16)){ret(uint32_t(std::toupper(int(value&0xff))),1);return;}
    for(uint32_t p=value;;++p){auto* c=static_cast<uint8_t*>(ptr(p,1));if(!*c)break;*c=uint8_t(std::toupper(*c));}
    ret(value,1);
}
// lstrcmpiA: case-insensitive, ASCII ordering (the game compares file and profile names).
void lstrcmpiA(){
    std::string a=lower(guestString(arg(0))),b=lower(guestString(arg(1)));
    int result=a.compare(b);ret(uint32_t(result<0?-1:result>0?1:0),2);
}
// CompareStringW -> CSTR_LESS_THAN 1, CSTR_EQUAL 2, CSTR_GREATER_THAN 3; NORM_IGNORECASE folds ASCII.
void compareStringW(){
    uint32_t flags=arg(1);
    auto read=[&](uint32_t text,int32_t count){std::u16string out;
        for(uint32_t i=0;count<0||i<uint32_t(count);++i){uint16_t c;std::memcpy(&c,ptr(text+i*2,2),2);if(count<0&&!c)break;
            if((flags&1)&&c>='A'&&c<='Z')c=uint16_t(c+32);out.push_back(char16_t(c));}
        return out;};
    auto a=read(arg(2),int32_t(arg(3))),b=read(arg(4),int32_t(arg(5)));
    int result=a.compare(b);ret(result<0?1u:result>0?3u:2u,6);
}
// GetDateFormatA / GetTimeFormatA with the en-US pictures (the game stamps profiles
// and saves). A null SYSTEMTIME means local now; cch 0 asks for the size.
std::array<uint16_t,8> localSystemTime(uint32_t address){
    std::array<uint16_t,8> t{};
    if(address){std::memcpy(t.data(),ptr(address,16),16);return t;}
    timespec now{};clock_gettime(CLOCK_REALTIME,&now);std::tm local{};time_t seconds=now.tv_sec;localtime_r(&seconds,&local);
    t={uint16_t(local.tm_year+1900),uint16_t(local.tm_mon+1),uint16_t(local.tm_wday),uint16_t(local.tm_mday),
       uint16_t(local.tm_hour),uint16_t(local.tm_min),uint16_t(local.tm_sec),uint16_t(now.tv_nsec/1000000)};
    return t;
}
std::string formatPicture(const std::string& picture,const std::array<uint16_t,8>& t){
    static const char* days[]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
    static const char* months[]={"January","February","March","April","May","June","July","August","September","October","November","December"};
    std::string out;
    for(size_t i=0;i<picture.size();){
        char c=picture[i];size_t n=1;while(i+n<picture.size()&&picture[i+n]==c)++n;
        auto two=[&](unsigned v,bool pad){char b[8];std::snprintf(b,sizeof b,pad?"%02u":"%u",v);out+=b;};
        if(c=='\''){size_t end=picture.find('\'',i+1);if(end==std::string::npos)end=picture.size();out+=picture.substr(i+1,end-i-1);i=end+1;continue;}
        unsigned hour12=t[4]%12?t[4]%12:12;
        switch(c){
        case 'd':if(n<=2)two(t[3],n==2);else{std::string d=days[t[2]%7];out+=n==3?d.substr(0,3):d;}break;
        case 'M':if(n<=2)two(t[1],n==2);else{std::string m=months[(t[1]+11)%12];out+=n==3?m.substr(0,3):m;}break;
        case 'y':if(n<=2)two(t[0]%100,n==2);else two(t[0],false);break;
        case 'h':two(hour12,n>=2);break;
        case 'H':two(t[4],n>=2);break;
        case 'm':two(t[5],n>=2);break;
        case 's':two(t[6],n>=2);break;
        case 't':out+=n==1?(t[4]<12?"A":"P"):(t[4]<12?"AM":"PM");break;
        default:out.append(n,c);
        }
        i+=n;
    }
    return out;
}
void putFormatted(const std::string& text,uint32_t buffer,uint32_t capacity,unsigned argumentCount){
    uint32_t need=uint32_t(text.size())+1;
    if(!capacity){ret(need,argumentCount);return;}
    if(capacity<need){lastError=122;ret(0,argumentCount);return;}
    std::memcpy(ptr(buffer,need),text.c_str(),need);ret(need,argumentCount);
}
void getDateFormatA(){
    auto t=localSystemTime(arg(2));
    std::string picture=arg(3)?guestString(arg(3)):(arg(1)&2?"dddd, MMMM d, yyyy":"M/d/yyyy");
    putFormatted(formatPicture(picture,t),arg(4),arg(5),6);
}
void getTimeFormatA(){
    auto t=localSystemTime(arg(2));uint32_t flags=arg(1);
    std::string picture=arg(3)?guestString(arg(3)):"h:mm:ss tt";
    if(flags&8)picture=arg(3)?picture:"HH:mm:ss";
    if(flags&1)picture=picture.substr(0,picture.find(':'))+(picture.find(' ')!=std::string::npos?picture.substr(picture.find(' ')):"");
    else if(flags&2){auto a=picture.find(":ss");if(a!=std::string::npos)picture.erase(a,3);}
    if(flags&4){auto a=picture.find(" tt");if(a!=std::string::npos)picture.erase(a,3);}
    putFormatted(formatPicture(picture,t),arg(4),arg(5),6);
}
// Saves check free space first: report the sector geometry of a roomy disk (2 GB free).
void getDiskFreeSpaceA(){
    const uint32_t values[]={8,512,524288,1048576};
    for(unsigned i=0;i<4;++i)if(arg(1+i))write32(arg(1+i),values[i]);
    ret(1,5);
}
const std::map<std::string,recomp_func_t> windowFunctions={
    {"MessageBoxA",messageBox},
    {"PeekMessageA",peekMessage},{"GetMessageA",getMessage},{"TranslateMessage",translateMessage},{"DispatchMessageA",dispatchMessage},
    {"PostMessageA",postMessage},{"PostQuitMessage",postQuitMessage},{"SendMessageA",sendMessage},
    {"wvsprintfA",winFormatA},
    {"SetCursor",setCursor},{"ShowCursor",showCursor},{"GetActiveWindow",getActiveWindow},{"GetForegroundWindow",getActiveWindow},{"SetActiveWindow",setActiveWindow},
    {"SetFocus",setActiveWindow},{"GetFocus",getActiveWindow},
    {"SetForegroundWindow",setForegroundWindow},
    {"BeginPaint",beginPaint},{"EndPaint",endPaint},
    {"AdjustWindowRect",adjustWindowRect},{"ShowWindow",showWindow},{"SetWindowPos",setWindowPos},{"UpdateWindow",updateWindow},
    {"RegisterClassExA",registerClassEx},{"CreateWindowExA",createWindowEx},{"DefWindowProcA",defaultWindowProc},
    {"GetWindowLongA",getWindowLong},{"SetWindowLongA",setWindowLong},
    {"LoadIconA",loadIcon},{"LoadCursorA",loadCursor},
    {"GetDesktopWindow",desktopHandle},{"DestroyWindow",destroyWindow},{"GetWindowRect",windowRect},
    {"SetCapture",setCapture},{"ReleaseCapture",releaseCapture},
    {"GetClientRect",clientRect},{"GetCursorPos",cursorPosition},{"GetSystemMetrics",systemMetrics},
    {"SetCursorPos",setCursorPos},{"CharUpperA",charUpperA}
};
// Guest threads run one at a time under the machine lock, so plain accesses are atomic to them.
void interlockedExchange(){uint32_t target=arg(0),old=read32(target);write32(target,arg(1));ret(old,2);}
void interlockedIncrement(){uint32_t target=arg(0),value=read32(target)+1;write32(target,value);ret(value,1);}
void interlockedDecrement(){uint32_t target=arg(0),value=read32(target)-1;write32(target,value);ret(value,1);}
void interlockedCompareExchange(){uint32_t target=arg(0),old=read32(target);if(old==arg(2))write32(target,arg(1));ret(old,3);}
const std::map<std::string,recomp_func_t> kernelFunctions={
    {"IsBadReadPtr",isBadReadPtr},{"IsBadWritePtr",isBadWritePtr},{"VirtualProtect",virtualProtect},
    {"CreateMutexA",createMutex},{"ReleaseMutex",releaseMutex},
    {"GetTimeZoneInformation",getTimeZoneInformation},
    {"GetProcessAffinityMask",getProcessAffinityMask},{"SetProcessAffinityMask",setProcessAffinityMask},
    {"GetPriorityClass",getPriorityClass},{"SetPriorityClass",setPriorityClass},{"GetThreadPriority",getThreadPriority},
    {"LoadLibraryA",loadLibrary},{"FreeLibrary",freeLibrary},
    {"SetErrorMode",setErrorMode},
    {"CreateEventA",createEvent},{"SetEvent",setEvent},{"ResetEvent",resetEvent},
    {"CreateThread",createThread},{"ResumeThread",resumeThread},{"GetExitCodeThread",threadExitCode},{"WaitForSingleObject",waitThread},{"WaitForMultipleObjects",waitMultiple},{"ExitThread",exitThread},{"TerminateThread",terminateThread},
    {"InterlockedExchange",interlockedExchange},{"InterlockedIncrement",interlockedIncrement},{"InterlockedDecrement",interlockedDecrement},{"InterlockedCompareExchange",interlockedCompareExchange},{"SetThreadPriority",setThreadPriority},
    {"CreateToolhelp32Snapshot",toolhelpSnapshot},{"Module32First",moduleFirst},{"Module32Next",moduleNext},{"CloseHandle",closeHandle},
    {"Process32First",processFirst},{"Process32Next",processNext},
    {"DuplicateHandle",duplicateHandle},
    {"GetProcAddress",getProcAddress},{"GetModuleHandleW",moduleHandleW},{"GetLastError",getLastError},{"SetLastError",setLastError},
    {"TlsAlloc",tlsAlloc},{"TlsFree",tlsFree},{"TlsGetValue",tlsGet},{"TlsSetValue",tlsSet},
    {"InitializeCriticalSection",initializeCritical},{"InitializeCriticalSectionAndSpinCount",initializeCriticalSpin},
    {"EnterCriticalSection",enterCritical},{"LeaveCriticalSection",leaveCritical},{"DeleteCriticalSection",deleteCritical},
    {"GetStartupInfoA",startupInfo},{"SetHandleCount",setHandleCount},{"GetCommandLineA",commandLine},{"GetEnvironmentStrings",environmentA},
    {"GetEnvironmentStringsA",environmentA},{"GetEnvironmentStringsW",environmentW},
    {"FreeEnvironmentStringsA",freeEnvironmentA},{"FreeEnvironmentStringsW",freeEnvironmentW},
    {"GetModuleFileNameA",moduleFileName},{"GetCurrentDirectoryA",currentDirectory},
    {"GetStdHandle",stdHandle},{"GetFileType",fileType},{"GetConsoleMode",consoleMode},
    {"GetCurrentProcess",currentProcess},{"GetCurrentThread",currentThread},{"GetCurrentProcessId",processId},
    {"GetCurrentThreadId",threadId},{"GetVersion",version},{"IsDebuggerPresent",isDebugger},{"IsProcessorFeaturePresent",processorFeature},
    {"SetUnhandledExceptionFilter",setExceptionFilter},{"EncodePointer",encodePointer},{"DecodePointer",encodePointer},
    {"GetACP",getACP},{"GetOEMCP",getOEMCP},{"GetCPInfo",cpInfo},{"GetThreadLocale",threadLocale},
    {"WideCharToMultiByte",wideToMulti},{"CompareStringW",compareStringW},{"GetDateFormatA",getDateFormatA},{"GetTimeFormatA",getTimeFormatA},{"GetDiskFreeSpaceA",getDiskFreeSpaceA},{"lstrcmpiA",lstrcmpiA},{"MultiByteToWideChar",multiToWide},{"GetStringTypeW",stringTypeW},{"LCMapStringW",mapStringW},
    {"GetTickCount",tickCount},{"QueryPerformanceCounter",performanceCounter},
    {"QueryPerformanceFrequency",performanceFrequency},{"GetSystemTimeAsFileTime",systemFileTime},
    {"FileTimeToLocalFileTime",fileTimeToLocalFileTime},{"FileTimeToSystemTime",fileTimeToSystemTime},
    {"GetSystemInfo",systemInfo},{"Sleep",sleepShim},{"SleepEx",sleepEx},{"ExitProcess",exitProcess},{"OutputDebugStringA",debugString}
};

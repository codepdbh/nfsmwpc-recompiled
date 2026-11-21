// Guest registers remain serialized; blocked native threads release the machine.
struct GuestThread {
    pthread_t native{};
    uint32_t id{},handle{},start{},parameter{},stack{},tib{},exitCode=259,suspended{};
    int32_t priority=0;
    bool started=false,done=false;
    bool timer=false;
    bool terminateRequested=false;uint32_t terminateCode=0;
};
struct GuestTimer {
    uint32_t id,delay,callback,parameter;bool periodic,cancelled=false,running=false,synchronous=false;
    std::chrono::steady_clock::time_point due;
};
std::map<uint32_t,std::shared_ptr<GuestTimer>> timerHandles;
std::shared_ptr<GuestThread> timerWorker;uint32_t nextTimerId=1;
thread_local uint32_t runningTimerId=0;
void runGuestTimers();
std::vector<std::shared_ptr<GuestThread>> guestThreads;
std::map<uint32_t,std::shared_ptr<GuestThread>> threadHandles;
uint32_t nextThreadId=2,nextThreadHandle=0xf0030000;
struct GuestEvent {bool manual,signaled;};
std::map<uint32_t,std::shared_ptr<GuestEvent>> eventHandles;
std::map<std::string,std::weak_ptr<GuestEvent>> namedEvents;
uint32_t nextEventHandle=0xf0040000;
struct GuestMutex {uint32_t owner=0,depth=0;bool abandoned=false;};
std::map<uint32_t,std::shared_ptr<GuestMutex>> mutexHandles;
std::map<std::string,std::weak_ptr<GuestMutex>> namedMutexes;
uint32_t nextMutexHandle=0xf0080000;
void createMutex(){
    auto name=guestString(arg(2));std::shared_ptr<GuestMutex> value;
    if(!name.empty())value=namedMutexes[name].lock();
    if(!name.empty()&&namedEvents.count(name)&&!namedEvents[name].expired()){lastError=6;ret(0,3);return;}
    lastError=value?183:0;
    if(!value){value=std::make_shared<GuestMutex>();if(arg(1)){value->owner=guestThreadId;value->depth=1;}if(!name.empty())namedMutexes[name]=value;}
    uint32_t handle=nextMutexHandle++;mutexHandles.emplace(handle,value);ret(handle,3);
}
void releaseMutex(){
    auto value=mutexHandles.find(arg(0));if(value==mutexHandles.end()){lastError=6;ret(0,1);return;}
    if(value->second->owner!=guestThreadId){lastError=288;ret(0,1);return;}
    if(!--value->second->depth){value->second->owner=0;signalGuestSync(value->second.get());}ret(1,1);
}
// Semaphores and waitable timers (Most Wanted's streaming and frame pacing).
struct GuestSemaphore{int32_t count,maximum;};
std::map<uint32_t,std::shared_ptr<GuestSemaphore>> semaphoreHandles;
uint32_t nextSemaphoreHandle=0xf00a0000;
void createSemaphore(){
    int32_t initial=int32_t(arg(1)),maximum=int32_t(arg(2));
    if(maximum<=0||initial<0||initial>maximum){lastError=87;ret(0,4);return;}
    uint32_t handle=nextSemaphoreHandle++;semaphoreHandles.emplace(handle,std::make_shared<GuestSemaphore>(GuestSemaphore{initial,maximum}));
    lastError=0;ret(handle,4);
}
void releaseSemaphore(){
    auto found=semaphoreHandles.find(arg(0));if(found==semaphoreHandles.end()){lastError=6;ret(0,3);return;}
    auto& value=*found->second;int32_t release=int32_t(arg(1));
    if(release<=0||value.count+release>value.maximum){lastError=298;ret(0,3);return;}  // ERROR_TOO_MANY_POSTS
    if(arg(2))write32(arg(2),uint32_t(value.count));
    value.count+=release;signalGuestSync(found->second.get());ret(1,3);
}
// A waitable timer is an event that the waits switch on once its due time passes.
struct GuestWaitTimer{std::chrono::steady_clock::time_point due;uint32_t periodMs=0;bool armed=false;};
std::map<uint32_t,GuestWaitTimer> waitTimers;
void createWaitableTimer(){
    uint32_t handle=nextEventHandle++;eventHandles.emplace(handle,std::make_shared<GuestEvent>(GuestEvent{arg(1)!=0,false}));
    waitTimers[handle]={};lastError=0;ret(handle,3);
}
void setWaitableTimer(){
    auto timer=waitTimers.find(arg(0));if(timer==waitTimers.end()){lastError=6;ret(0,6);return;}
    int64_t due=int64_t(uint64_t(read32(arg(1)))|uint64_t(read32(arg(1)+4))<<32);
    auto now=std::chrono::steady_clock::now();
    if(due<0)timer->second.due=now+std::chrono::microseconds(-due/10);
    else{  // absolute FILETIME (UTC)
        int64_t unix100ns=due-116444736000000000LL;auto wall=std::chrono::system_clock::now().time_since_epoch();
        auto delta=std::chrono::microseconds(unix100ns/10)-std::chrono::duration_cast<std::chrono::microseconds>(wall);
        timer->second.due=now+(delta.count()>0?delta:std::chrono::microseconds(0));
    }
    timer->second.periodMs=arg(2);timer->second.armed=true;auto& event=eventHandles[arg(0)];event->signaled=false;
    if(arg(3))throw GuestStop("SetWaitableTimer completion routine pending");
    signalGuestSync(event.get());   // a wait on it re-reads the due time (waitTimerDue)
    ret(1,6);
}
void pollWaitTimer(uint32_t handle){
    auto timer=waitTimers.find(handle);if(timer==waitTimers.end()||!timer->second.armed)return;
    if(std::chrono::steady_clock::now()<timer->second.due)return;
    eventHandles[handle]->signaled=true;
    if(timer->second.periodMs)timer->second.due+=std::chrono::milliseconds(timer->second.periodMs);else timer->second.armed=false;
}
// When a wait on this handle has to look again without a signal: an armed
// waitable timer turns its event on by time alone (pollWaitTimer).
std::chrono::steady_clock::time_point waitTimerDue(uint32_t handle){
    auto timer=waitTimers.find(handle);
    return timer==waitTimers.end()||!timer->second.armed?noDeadline:timer->second.due;
}
void createEvent(){
    auto name=guestString(arg(3));std::shared_ptr<GuestEvent> event;
    if(!name.empty()&&namedMutexes.count(name)&&!namedMutexes[name].expired()){lastError=6;ret(0,4);return;}
    if(!name.empty())event=namedEvents[name].lock();
    lastError=event?183:0;
    if(!event){event=std::make_shared<GuestEvent>(GuestEvent{arg(1)!=0,arg(2)!=0});if(!name.empty())namedEvents[name]=event;}
    uint32_t handle=nextEventHandle++;eventHandles.emplace(handle,event);ret(handle,4);
}
void setEvent(){auto event=eventHandles.find(arg(0));if(event==eventHandles.end()){lastError=6;ret(0,1);return;}event->second->signaled=true;signalGuestSync(event->second.get());ret(1,1);}
void resetEvent(){auto event=eventHandles.find(arg(0));if(event==eventHandles.end()){lastError=6;ret(0,1);return;}event->second->signaled=false;ret(1,1);}
// ExitThread unwinds the lifted frames back to guestThreadEntry.
struct GuestThreadExit{uint32_t code;};
thread_local bool insideGuestThread=false;
thread_local GuestThread* currentGuestThread=nullptr;
void checkGuestThreadCancellation(){
    if(currentGuestThread&&currentGuestThread->terminateRequested)
        throw GuestThreadExit{currentGuestThread->terminateCode};
}
void terminateThread(){
    auto found=threadHandles.find(arg(0));
    auto* target=arg(0)==0xfffffffeu?currentGuestThread:found==threadHandles.end()?nullptr:found->second.get();
    if(!target){lastError=6;ret(0,2);return;}
    auto& thread=*target;
    if(!thread.done&&!thread.terminateRequested){
        thread.terminateCode=arg(1);thread.terminateRequested=true;
        NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSMW","Terminating guest thread %u exit=%u",thread.id,thread.terminateCode);
        signalGuestSync();   // the target may be blocked on any object; it checks for cancellation on waking
    }
    checkGuestThreadCancellation(); // Self-termination never returns.
    ret(1,2);
}
void exitThread(){
    if(!insideGuestThread)throw GuestStop("ExitThread en el hilo principal desde "+hex(g_cur_func));
    throw GuestThreadExit{arg(0)};
}
void* guestThreadEntry(void* context){
    stayOnPerformanceCores();
    insideGuestThread=true;
    auto& thread=*static_cast<GuestThread*>(context);
    currentGuestThread=&thread;
    {char name[16];std::snprintf(name,sizeof name,"guest-%u",thread.id);pthread_setname_np(pthread_self(),name);}
    std::unique_lock<MachineMutex> lease(machine);machineLease=&lease;guestThreadId=thread.id;
    State fresh;fresh.esp=thread.stack+(4u<<20)-64;fresh.fs=thread.tib;fresh.load();
    try {
        if(thread.suspended)blockGuestUntil([&]{return !thread.suspended;},noDeadline,{&thread});
        if(thread.timer){
            runGuestTimers();
            thread.exitCode=0;
        }else thread.exitCode=callGuest(thread.start,{thread.parameter},0,true);
    }catch(const GuestThreadExit& exit){
        thread.exitCode=exit.code;
    }catch(const GuestStop& error){
        thread.exitCode=1;
        if(!stopRequested){guestThreadFault=error.what();NFS_RUNTIME_LOG(ANDROID_LOG_WARN,"NFSMW","Guest thread %u: %s",thread.id,error.what());}
    }catch(const std::exception& error){thread.exitCode=1;guestThreadFault=error.what();}
    heaps->free(GuestHeaps::process,thread.stack);heaps->free(GuestHeaps::process,thread.tib);
    for(auto& value:mutexHandles)if(value.second->owner==thread.id){value.second->owner=0;value.second->depth=0;value.second->abandoned=true;signalGuestSync(value.second.get());}
    forgetThreadTrace();thread.done=true;signalGuestSync(&thread);currentGuestThread=nullptr;machineLease=nullptr;return nullptr;
}
void reapGuestThreads(){
    for(auto it=guestThreads.begin();it!=guestThreads.end();){
        auto& thread=*it;
        if(thread->done){if(thread->started){pthread_join(thread->native,nullptr);thread->started=false;}it=guestThreads.erase(it);}
        else ++it;
    }
}
void createThread(){
    reapGuestThreads();
    uint32_t flags=arg(4),idOut=arg(5);
    if(flags&~4u){lastError=87;ret(0,6);return;}
    auto thread=std::make_shared<GuestThread>();thread->id=nextThreadId++;thread->handle=nextThreadHandle++;
    thread->start=arg(2);thread->parameter=arg(3);thread->suspended=flags&4?1:0;
    if(!recomp_lookup(thread->start)&&!recomp_lookup_manual(thread->start)&&!recomp_lookup_import(thread->start)){lastError=87;ret(0,6);return;}
    thread->stack=allocate(4u<<20);thread->tib=allocate(4096);
    write32(thread->tib,UINT32_MAX);write32(thread->tib+4,thread->stack+(4u<<20));write32(thread->tib+8,thread->stack);
    write32(thread->tib+0x18,thread->tib);write32(thread->tib+0x20,1);write32(thread->tib+0x24,thread->id);
    pthread_attr_t attributes;int error=pthread_attr_init(&attributes);
    if(!error){error=pthread_attr_setstacksize(&attributes,64u<<20);if(!error)error=pthread_create(&thread->native,&attributes,guestThreadEntry,thread.get());pthread_attr_destroy(&attributes);}
    if(error){heaps->free(GuestHeaps::process,thread->stack);heaps->free(GuestHeaps::process,thread->tib);lastError=8;ret(0,6);return;}
    thread->started=true;guestThreads.push_back(thread);threadHandles.emplace(thread->handle,thread);
    if(idOut)write32(idOut,thread->id);
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSMW","Created guest thread %u at %08x suspended=%u",thread->id,thread->start,thread->suspended);
    ret(thread->handle,6);
}
void resumeThread(){auto thread=threadHandles.find(arg(0));if(thread==threadHandles.end()){lastError=6;ret(UINT32_MAX,1);return;}uint32_t old=thread->second->suspended;if(old&&!--thread->second->suspended)signalGuestSync(thread->second.get());ret(old,1);}
void setThreadPriority(){
    uint32_t handle=arg(0);int32_t priority=static_cast<int32_t>(arg(1));
    if(priority< -2&&priority!= -15){lastError=87;ret(0,2);return;}if(priority>2&&priority!=15){lastError=87;ret(0,2);return;}
    auto thread=threadHandles.find(handle);
    if(thread==threadHandles.end()){lastError=6;ret(0,2);return;}
    thread->second->priority=priority;ret(1,2);
}
void threadExitCode(){auto thread=threadHandles.find(arg(0));if(thread==threadHandles.end()){lastError=6;ret(0,2);return;}write32(arg(1),thread->second->exitCode);ret(1,2);}
void waitThread(){
    uint32_t handle=arg(0),timeout=arg(1);auto found=threadHandles.find(handle);
    WaitScope scope(mutexHandles.count(handle)?"WaitForSingleObject(mutex)":eventHandles.count(handle)?"WaitForSingleObject(event)":"WaitForSingleObject(thread)",handle);
    auto mutexFound=mutexHandles.find(handle);
    if(mutexFound!=mutexHandles.end()){
        auto value=mutexFound->second;
        if(!blockGuestUntil([&]{return !value->owner||value->owner==guestThreadId;},guestDeadline(timeout),{value.get()})){ret(258,2);return;}
        value->owner=guestThreadId;++value->depth;bool abandoned=value->abandoned;value->abandoned=false;ret(abandoned?128:0,2);return;
    }
    if(auto semaphore=semaphoreHandles.find(handle);semaphore!=semaphoreHandles.end()){
        auto value=semaphore->second;
        if(!blockGuestUntil([&]{return value->count>0;},guestDeadline(timeout),{value.get()})){ret(258,2);return;}
        --value->count;ret(0,2);return;
    }
    auto eventFound=eventHandles.find(handle);
    if(found==threadHandles.end()&&eventFound==eventHandles.end()){lastError=6;ret(UINT32_MAX,2);return;}
    auto thread=found==threadHandles.end()?nullptr:found->second;
    auto event=eventFound==eventHandles.end()?nullptr:eventFound->second;
    const void* key=thread?static_cast<const void*>(thread.get()):static_cast<const void*>(event.get());
    if(!blockGuestUntil([&]{return thread?thread->done:(pollWaitTimer(handle),event->signaled);},guestDeadline(timeout),&key,1,
        [&]{return thread?noDeadline:waitTimerDue(handle);})){ret(258,2);return;}
    if(event&&!event->manual)event->signaled=false;
    ret(0,2);
}
// WaitForMultipleObjects over the same thread, event and mutex objects as WaitForSingleObject.
enum class WaitableState { invalid,waiting,ready };
WaitableState waitableState(uint32_t handle){
    bool ready;
    if(auto mutex=mutexHandles.find(handle);mutex!=mutexHandles.end())ready=!mutex->second->owner||mutex->second->owner==guestThreadId;
    else if(auto semaphore=semaphoreHandles.find(handle);semaphore!=semaphoreHandles.end())ready=semaphore->second->count>0;
    else if(auto event=eventHandles.find(handle);event!=eventHandles.end()){pollWaitTimer(handle);ready=event->second->signaled;}
    else if(auto thread=threadHandles.find(handle);thread!=threadHandles.end())ready=thread->second->done;
    else return WaitableState::invalid;
    return ready?WaitableState::ready:WaitableState::waiting;
}
// The object a wait on this handle sleeps on (signalGuestSync key); valid handles only.
const void* waitableKey(uint32_t handle){
    if(auto mutex=mutexHandles.find(handle);mutex!=mutexHandles.end())return mutex->second.get();
    if(auto semaphore=semaphoreHandles.find(handle);semaphore!=semaphoreHandles.end())return semaphore->second.get();
    if(auto event=eventHandles.find(handle);event!=eventHandles.end())return event->second.get();
    return threadHandles.at(handle).get();
}
bool acquireWaitable(uint32_t handle){  // true when an abandoned mutex was taken
    if(auto mutex=mutexHandles.find(handle);mutex!=mutexHandles.end()){
        auto& value=*mutex->second;value.owner=guestThreadId;++value.depth;bool abandoned=value.abandoned;value.abandoned=false;return abandoned;
    }
    if(auto semaphore=semaphoreHandles.find(handle);semaphore!=semaphoreHandles.end()){--semaphore->second->count;return false;}
    if(auto event=eventHandles.find(handle);event!=eventHandles.end()&&!event->second->manual)event->second->signaled=false;
    return false;
}
void waitMultiple(){
    uint32_t count=arg(0),list=arg(1),timeout=arg(3);bool all=arg(2)!=0;
    if(!count||count>64||!list){lastError=87;ret(UINT32_MAX,4);return;}
    std::vector<uint32_t> handles(count);
    for(uint32_t i=0;i<count;++i){
        handles[i]=read32(list+i*4);
        if(waitableState(handles[i])==WaitableState::invalid){lastError=6;ret(UINT32_MAX,4);return;}
    }
    auto deadline=guestDeadline(timeout);
    WaitScope scope(all?"WaitForMultipleObjects(all)":"WaitForMultipleObjects(any)",handles[0]);
    std::vector<const void*> keys(count);for(uint32_t i=0;i<count;++i)keys[i]=waitableKey(handles[i]);
    // Wakes the blocked wait for the full evaluation below: the wait can be
    // satisfied, or a handle was closed meanwhile (reported as WAIT_FAILED).
    auto changed=[&]{
        pollGuestNetwork();
        bool every=true,some=false;
        for(uint32_t handle:handles){
            auto state=waitableState(handle);if(state==WaitableState::invalid)return true;
            every&=state==WaitableState::ready;some|=state==WaitableState::ready;
        }
        return all?every:some;
    };
    auto timerDue=[&]{auto due=noDeadline;for(uint32_t handle:handles)due=std::min(due,waitTimerDue(handle));return due;};
    for(;;){
        pollGuestNetwork();
        // Online teardown can close an event while this wait releases the
        // machine. Validate every object again, including ones after a ready
        // entry, before consuming any signal. Closing a handle during a wait
        // is undefined on Windows; report WAIT_FAILED instead of stopping the
        // entire game with std::map::at (or treating a missing event as a thread).
        std::array<WaitableState,64> states;
        bool ready=true;
        for(uint32_t i=0;i<count;++i){
            states[i]=waitableState(handles[i]);
            if(states[i]==WaitableState::invalid){
                NFS_RUNTIME_LOG(ANDROID_LOG_WARN,"NFSMW","WaitForMultipleObjects: handle %08x closed during wait; returning ERROR_INVALID_HANDLE",handles[i]);
                lastError=6;ret(UINT32_MAX,4);return;
            }
            ready&=states[i]==WaitableState::ready;
        }
        if(all){
            if(ready){
                bool abandoned=false;for(uint32_t handle:handles)abandoned|=acquireWaitable(handle);
                ret(abandoned?128:0,4);return;
            }
        }else for(uint32_t i=0;i<count;++i)if(states[i]==WaitableState::ready){ret((acquireWaitable(handles[i])?128:0)+i,4);return;}
        if(std::chrono::steady_clock::now()>=deadline){ret(258,4);return;}
        blockGuestUntil(changed,deadline,keys.data(),keys.size(),timerDue);   // either way the loop re-evaluates
    }
}
void joinGuestThreads(std::unique_lock<MachineMutex>& lease){
    stopRequested=true;signalGuestSync();lease.unlock();
    for(auto& thread:guestThreads)if(thread->started){pthread_join(thread->native,nullptr);thread->started=false;}
    lease.lock();
}
void runGuestTimers(){
    auto earliest=[]{
        std::shared_ptr<GuestTimer> next;
        for(const auto& entry:timerHandles)if(!entry.second->cancelled&&(!next||entry.second->due<next->due))next=entry.second;
        return next;
    };
    while(!stopRequested){
        checkGuestProgress();auto next=earliest();
        if(!next||std::chrono::steady_clock::now()<next->due){
            // Sleep until the earliest timer is due, or until timeSetEvent /
            // timeKillEvent changed which timer that is.
            blockGuestUntil([&]{return earliest()!=next;},next?next->due:noDeadline,{&timerHandles});
            continue;
        }
        next->running=true;runningTimerId=next->id;
        try{callGuest(next->callback,{next->id,0,next->parameter,0,0});}
        catch(...){next->running=false;runningTimerId=0;throw;}
        next->running=false;runningTimerId=0;
        if(next->periodic&&!next->cancelled){next->due=std::chrono::steady_clock::now()+std::chrono::milliseconds(next->delay);}
        else timerHandles.erase(next->id);
        signalGuestSync(next.get());   // synchronous timeKillEvent waits for running=false
    }
}
void timeSetEvent(){
    uint32_t flags=arg(4),delay=arg(0),callback=arg(2);
    if(flags&~0x101u)throw GuestStop("timeSetEvent callback flags pending: "+hex(flags));
    if(!delay||(!recomp_lookup(callback)&&!recomp_lookup_manual(callback))){ret(0,5);return;}
    uint32_t id=nextTimerId++;auto timer=std::make_shared<GuestTimer>();
    timer->id=id;timer->delay=delay;timer->callback=callback;timer->parameter=arg(3);timer->periodic=flags&1;timer->synchronous=flags&0x100;
    timer->due=std::chrono::steady_clock::now()+std::chrono::milliseconds(delay);timerHandles.emplace(id,timer);
    if(timerWorker){if(timerWorker->done){timerHandles.erase(id);ret(0,5);return;}
        signalGuestSync(&timerHandles);
        if(id%500==0)NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSMW","Guest timer %u reuses worker thread %u",id,timerWorker->id);ret(id,5);return;}
    auto thread=std::make_shared<GuestThread>();thread->id=nextThreadId++;thread->timer=true;
    thread->stack=allocate(4u<<20);thread->tib=allocate(4096);
    write32(thread->tib,UINT32_MAX);write32(thread->tib+4,thread->stack+(4u<<20));write32(thread->tib+8,thread->stack);
    write32(thread->tib+0x18,thread->tib);write32(thread->tib+0x20,1);write32(thread->tib+0x24,thread->id);
    pthread_attr_t attributes;int error=pthread_attr_init(&attributes);
    if(!error){error=pthread_attr_setstacksize(&attributes,64u<<20);if(!error)error=pthread_create(&thread->native,&attributes,guestThreadEntry,thread.get());pthread_attr_destroy(&attributes);}
    if(error){heaps->free(GuestHeaps::process,thread->stack);heaps->free(GuestHeaps::process,thread->tib);timerHandles.erase(id);ret(0,5);return;}
    thread->started=true;guestThreads.push_back(thread);timerWorker=thread;
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSMW","Created persistent guest timer worker thread %u",thread->id);ret(id,5);
}
void timeKillEvent(){
    auto timer=timerHandles.find(arg(0));if(timer==timerHandles.end()){ret(97,1);return;}
    auto value=timer->second;
    if(value->running&&runningTimerId==value->id&&value->synchronous)throw GuestStop("Synchronous timer self-cancellation pending");
    value->cancelled=true;signalGuestSync(&timerHandles);
    if(value->synchronous)blockGuestUntil([&]{return !value->running;},noDeadline,{value.get()});
    timerHandles.erase(value->id);signalGuestSync(&timerHandles);ret(0,1);
}

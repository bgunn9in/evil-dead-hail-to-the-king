"""Read-only PC global50 playback research oracle; never executes original code."""
from dataclasses import dataclass
from copy import deepcopy
import hashlib
import struct

from ed_actions import EXE_SHA256,_va_reader


class PoolFull(RuntimeError):pass
class UndefinedSourceState(RuntimeError):pass


@dataclass(frozen=True)
class Clip:
    animation:int=1
    start:int=0
    end:int=1
    loop_start:int=0
    loop_end:int=1
    early_end:int=1
    finish_start:int=1
    flags:int=0
    begin_event:int=0
    end_event:int=0


@dataclass
class Slot:
    owner:tuple|None=None
    clip:Clip|None=None
    active:bool=False
    reusable:bool=True
    frame:int=0
    previous_frame:int=0
    mode:int=0
    desired_mode:int=0
    step:int=1
    parent:int|None=None
    child:int|None=None
    begin_event:int=0
    end_event:int=0
    loop_event:int=0


class GlobalPool:
    """Semantic valid-state oracle, not old renderer/context RAM emulation.

    PoolFull is a bounded compatibility diagnostic:405818 otherwise writes slot50.
    Opaque actor keys represent fixed native(type,index) wrappers, not draw slots.
    """
    def __init__(self):
        self.slots=[Slot()for _ in range(50)]
        self.flags={};self.registered=set();self.messages=[]

    def add_actor(self,owner,flags=1,registered=True):
        self.flags[owner]=flags
        if registered:self.registered.add(owner)

    def unregister(self,owner):
        self.registered.discard(owner) #402E80 never touches animation records.

    def message(self,owner,index,event):
        if event:self.messages.append((owner,index,event))

    def allocate(self,owner,clip=Clip(),mode=0,priority=False):
        if not clip.animation:return None
        first=0
        if priority:
            own=[n for n in range(25,50)if self.slots[n].active and self.slots[n].owner==owner]
            first=max(own)+1 if own else 25
        index=next((n for n in range(first,50)if not self.slots[n].active and self.slots[n].reusable),None)
        if index is None:raise PoolFull("Source4057A0 would write beyond global slot49")
        slot=Slot(owner,clip,True,True,clip.loop_start if mode==2 else clip.start,
                  clip.loop_start if mode==2 else clip.start,mode,mode)
        slot.begin_event=clip.begin_event;slot.end_event=clip.end_event
        slot.loop_event=clip.end_event if clip.flags&0x100 else 0
        self.slots[index]=slot;self.message(owner,index,slot.begin_event)
        return index

    def overwrite(self,index,clip,mode):
        old=self.slots[index]
        self.message(old.owner,index,old.end_event) #4058E0 oldend precedes newbegin.
        new=Slot(old.owner,clip,True,True,clip.loop_start if mode==2 else clip.start,
                 clip.loop_start if mode==2 else clip.start,mode,mode)
        new.parent=old.parent #4058E0 preserves+04; only child+28 is cleared.
        new.begin_event=clip.begin_event;new.end_event=clip.end_event
        new.loop_event=clip.end_event if clip.flags&0x100 else 0
        self.slots[index]=new;self.message(new.owner,index,new.begin_event)

    def disable(self,index):
        self.slots[index].active=False;self.slots[index].reusable=True #418704 noend.

    def link(self,parent,clip,desired_mode=0):
        old=self.slots[parent]
        child=self.allocate(old.owner,clip,4,False) #4059F0 always low-priority.
        if child is None:raise UndefinedSourceState("Source child dereferences a null animation")
        old.child=child;new=self.slots[child];new.parent=parent;new.desired_mode=desired_mode
        if desired_mode==2:new.frame=clip.loop_start
        if old.clip.flags&4 and old.frame<=old.clip.early_end:old.mode=6
        else:
            old.frame=old.clip.finish_start;old.mode=3
            old.step=1 if old.clip.end>=old.frame else -1
        return child

    def tick(self,pre_step=None):
        observed=[]
        for index in range(50):
            s=self.slots[index]
            if not s.active or s.mode==4 or not self.flags.get(s.owner,0)&1 or self.flags[s.owner]&0x40:continue
            observed.append((s.owner,index,s.frame))
            if pre_step:pre_step(s.owner,index,s.frame,self)
            c=s.clip;s.previous_frame=s.frame;s.frame+=s.step
            if s.mode==6 and s.frame>c.early_end:
                s.frame=c.finish_start;s.mode=3;s.step=1 if c.end>=s.frame else -1
            if s.mode==1 and s.frame>=c.loop_start:
                s.mode=2;self.message(s.owner,index,s.loop_event)
            if s.mode==2:
                if s.frame>c.loop_end:s.frame=c.loop_start
                continue
            limit=c.early_end if c.flags&8 else c.end
            done=s.mode==5 or(s.mode==0 and s.frame>limit)or(s.mode==3 and(s.frame>c.end if s.step>0 else s.frame<c.end))
            if not done:continue
            s.active=False;self.message(s.owner,index,s.end_event)
            if s.child is not None:
                child=self.slots[s.child]
                if child.child is not None:child.mode=3;child.frame=child.clip.finish_start
                else:child.mode=child.desired_mode
                s.reusable=True
            else:s.reusable=False #Protected until406560, across actor/object updates.
        return observed

    def complete(self,on39=None):
        sent=[]
        for index in range(50):
            s=self.slots[index]
            if s.active or s.reusable:continue
            if s.owner is None:raise UndefinedSourceState("406560 would call418670 with null actor")
            owner=s.owner;s.reusable=True;sent.append((owner,index,39))
            if on39:on39(owner,index,self)
        return sent

    def clear_actor(self,owner):
        for s in self.slots:
            if s.owner==owner:s.active=False;s.owner=None;s.child=None #405AD0 retains readyword.

    def clear_except_type(self,actor_type):
        for s in self.slots:
            if s.owner is None or s.owner[0]!=actor_type:
                s.active=False;s.reusable=True;s.owner=None;s.child=None
                s.begin_event=s.end_event=s.loop_event=0 #4055C0; other stale fields are unused.

    def snapshot(self):return deepcopy((self.slots,self.flags,self.registered,self.messages))


def prove_source(exe):
    if hashlib.sha256(exe).hexdigest()!=EXE_SHA256:raise ValueError("Only the recorded PC executable is audited")
    r=_va_reader(exe)
    assert struct.unpack("<I",r(0x4057C6,4))[0]==25
    assert struct.unpack("<I",r(0x4057F9,4))[0]==0x475830
    assert r(0x4057FF,1)==bytes([50])
    assert r(0x40A7A0,1)==bytes([0xC3])
    ranges={"initialize":(0x405500,0x405599),"allocator":(0x4057A0,0x4058D1),"overwrite":(0x4058E0,0x4059F0),
            "link_child":(0x4059F0,0x405AB2),"clear_actor":(0x405AD0,0x405AF3),
            "scene_clear_loop":(0x4055C0,0x405602),"tick":(0x4060E0,0x40654E),"completion":(0x406560,0x4065A6),
            "unregister13":(0x402E80,0x402EAF),"destroy":(0x403900,0x403957)}
    code=r(0x401000,0x55300);calls={}
    for target in(0x405AD0,0x4057A0):
        calls[f"{target:08x}"]=[0x401000+k for k,b in enumerate(code[:-4])if b==0xE8 and 0x401000+k+5+struct.unpack_from("<i",code,k+1)[0]==target]
    return {"executable_sha256":EXE_SHA256,"pool_base":0x475830,"stride":48,"capacity":50,"priority_start":25,
            "routine_hashes":{name:{"start":a,"end_exclusive":b,"sha256":hashlib.sha256(r(a,b-a)).hexdigest()}for name,(a,b)in ranges.items()},"direct_call_sites":calls}

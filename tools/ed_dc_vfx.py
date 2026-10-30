"""Extract pinned original Dreamcast VFX parameters into the existing EDVFX ABI.

Reads actual source opcodes; fail closed outside the small straight-line constructor
instruction set. Three constructor calls are bounded hooks: modifier allocation,
memset, and the verified sprite-grid setup. The spawn callback is captured before
emission. No particle updates or RNG run here.
"""
from pathlib import Path
import argparse, hashlib, json, struct
from ed_dc_audio import checked_output as checked_original_output

SHA = 'de26cba0d4e6035d1128daf504a2257a695050bac11ecdf6e9e2e0baf0f26841'
ATLAS_SHA = 'c4680df67aefe141e72b488d6aaac4eb51ea3c1d0f2a15dbb826092d51fde9a2'
BASE = 0x8c010000
SUPPORTED = {0,2,3,6,7,8,9,15,16,17,18,19,20,21,22,23,24,25,32}
def signed(x, bits=32): return x-(1<<bits) if x&(1<<(bits-1)) else x
def f32(b): return struct.unpack('<f', struct.pack('<I', b))[0]

class Constructor:
    def __init__(self, source, kind, start):
        if hashlib.sha256(source).hexdigest() != SHA:
            raise ValueError('Dreamcast VFX requires the pinned original 1ST_READ.BIN')
        self.source, self.kind, self.pc = source, kind, start
        self.memory, self.writes, self.modifiers, self.calls, self.trace = {}, [], [], [], []
        self.r, self.fr = [0]*16, [0]*16
        self.r[4], self.r[5], self.r[15] = 0x20000000, 0x20001000, 0x30010000
        self.pr, self.t, self.step, self.macl, self.budget = 0, False, 0, 0, 2000
        self.dispatched = None

    def read(self, at, width):
        out=bytearray()
        for p in range(at,at+width):
            if p in self.memory: out.append(self.memory[p])
            elif BASE <= p < BASE+len(self.source): out.append(self.source[p-BASE])
            elif 0x8c000000 <= p < 0x8d000000 or 0x20000000 <= p < 0x30020000: out.append(0)
            else: raise ValueError(f'unmapped constructor read {p:08x}')
        return int.from_bytes(out,'little')

    def write(self, at, value, width):
        value &= (1<<(width*8))-1
        if not (0x20000000 <= at < 0x30020000 or 0x8c000000 <= at < 0x8d000000):
            raise ValueError(f'bad constructor write {at:08x}')
        for i in range(width): self.memory[at+i]=(value>>(8*i))&255
        self.writes.append([hex(self.pc),hex(at),width,hex(value)])

    def hook(self, target):
        self.calls.append([hex(self.pc),hex(target),*[hex(v) for v in self.r[4:7]]])
        if target == 0x8c06da40:
            mod=self.modifiers[0]
            self.dispatched=bytes(self.read(mod['payload']+i,1) for i in range(mod['size']))
        elif target == 0x8c06c720:
            typ=self.r[5]
            size=self.read(0x8c0c6acc+typ*8,4)
            if not 4 <= size <= 0x200: raise ValueError(f'bad modifier size {typ}: {size}')
            at=0x21000000+len(self.modifiers)*0x400
            payload=at+0x100
            self.write(at,typ,4)
            self.write(at+4,self.read(0x8c0c6c94+typ*4,4),4)
            self.write(at+8,payload,4)
            for p in range(payload,payload+size): self.memory[p]=0
            self.modifiers.append(dict(type=typ,node=at,payload=payload,size=size))
            self.r[0]=at
        elif target == 0x8c0af960:
            at,val,size=self.r[4:7]
            if size > 0x200: raise ValueError('memset too large')
            for p in range(at,at+size): self.write(p,val,1)
            self.r[0]=at
        elif target == 0x8c08c0a0:
            at=self.r[4]
            grid=self.read(at+0x11,1)
            layout={0:(1,1),1:(4,1),3:(2,1),4:(2,2),5:(2,3),6:(2,4),7:(3,1),8:(3,3),9:(3,4),10:(4,4),11:(4,8),12:(8,8),13:(16,1)}
            if grid not in layout: raise ValueError(f'unverified sprite grid {grid}')
            x,y=layout[grid]
            self.write(at+0x31,x*y,1); self.write(at+0x32,x,1); self.write(at+0x33,y,1)
            # Computed floating increments are irrelevant to exported definition fields.
        else: raise ValueError(f'unverified constructor call {target:08x} at {self.pc:08x}')

    def one(self, pc, delay=False):
        self.pc=pc; op=self.read(pc,2); n=(op>>8)&15; m=(op>>4)&15
        self.trace.append([hex(pc),f'{op:04x}'])
        self.step+=1
        if self.step>self.budget: raise ValueError('constructor instruction budget exceeded')
        nxt=pc+2
        if op==0x0009: pass
        elif op>>12==0xe: self.r[n]=signed(op&255,8)&0xffffffff
        elif op>>12==0x7: self.r[n]=(self.r[n]+signed(op&255,8))&0xffffffff
        elif op&0xf00f==0x6003: self.r[n]=self.r[m]
        elif op&0xf00f==0x300c: self.r[n]=(self.r[n]+self.r[m])&0xffffffff
        elif op&0xf00f==0x3008: self.r[n]=(self.r[n]-self.r[m])&0xffffffff
        elif op>>12==0xd: self.r[n]=self.read((pc+4 & ~3)+(op&255)*4,4)
        elif op>>12==0x9: self.r[n]=signed(self.read(pc+4+(op&255)*2,2),16)&0xffffffff
        elif op&0xff00==0xc700: self.r[0]=(pc+4 & ~3)+(op&255)*4
        elif op&0xf00f in (0x6000,0x6001,0x6002,0x6004,0x6005,0x6006):
            code=op&15; width=(1,2,4)[code%4]; val=self.read(self.r[m],width)
            self.r[n]=signed(val,width*8)&0xffffffff if width<4 else val
            if code>=4 and n!=m: self.r[m]+=width
        elif op&0xf00f in (0x2000,0x2001,0x2002,0x2004,0x2005,0x2006):
            code=op&15; width=(1,2,4)[code%4]
            if code>=4: self.r[n]=(self.r[n]-width)&0xffffffff
            self.write(self.r[n],self.r[m],width)
        elif op>>12==5: self.r[n]=self.read(self.r[m]+(op&15)*4,4)
        elif op>>12==1: self.write(self.r[n]+(op&15)*4,self.r[m],4)
        elif op&0xf00f==0x000e:self.r[n]=self.read(self.r[m]+self.r[0],4)
        elif op&0xf00f==0x000d:self.r[n]=signed(self.read(self.r[m]+self.r[0],2),16)&0xffffffff
        elif op&0xf00f==0x0006:self.write(self.r[n]+self.r[0],self.r[m],4)
        elif op&0xf00f==0x2008: self.t=(self.r[n]&self.r[m])==0
        elif op&0xf00f==0x200b:self.r[n]|=self.r[m]
        elif op&0xf00f==0x200a:self.r[n]^=self.r[m]
        elif op&0xf00f==0x0007:self.macl=(self.r[n]*self.r[m])&0xffffffff
        elif op&0xf0ff==0x001a:self.r[n]=self.macl
        elif op&0xf0ff==0x0029:self.r[n]=int(self.t)
        elif op&0xf0ff==0x4008:self.r[n]=(self.r[n]<<2)&0xffffffff
        elif op&0xf0ff==0x4010:self.r[n]=(self.r[n]-1)&0xffffffff;self.t=self.r[n]==0
        elif op&0xff00==0x8800:self.t=self.r[0]==(signed(op&255,8)&0xffffffff)
        elif op&0xf00f==0x3007:self.t=signed(self.r[n])>signed(self.r[m])
        elif op&0xf00f==0x3003:self.t=signed(self.r[n])>=signed(self.r[m])
        elif op&0xf00f==0x3006:self.t=self.r[n]>self.r[m]
        elif op&0xf0ff==0x4022: self.r[n]-=4;self.write(self.r[n],self.pr,4)
        elif op&0xf0ff==0x4026: self.pr=self.read(self.r[n],4);self.r[n]+=4
        elif op&0xf0ff==0xf08d: self.fr[n]=0
        elif op&0xf0ff==0xf09d: self.fr[n]=0x3f800000
        elif op&0xf00f==0xf00c: self.fr[n]=self.fr[m]
        elif op&0xf00f in (0xf001,0xf002):
            a,b=f32(self.fr[n]),f32(self.fr[m])
            self.fr[n]=struct.unpack('<I',struct.pack('<f',a-b if op&15==1 else a*b))[0]
        elif op&0xf00f in (0xf008,0xf009):
            self.fr[n]=self.read(self.r[m],4)
            if op&15==9:self.r[m]+=4
        elif op&0xf00f in (0xf00a,0xf00b):
            if op&15==11:self.r[n]-=4
            self.write(self.r[n],self.fr[m],4)
        elif op&0xf0ff==0x400b:
            if delay: raise ValueError('branch in delay slot')
            target=self.r[n];self.pr=pc+4;self.one(pc+2,True);self.hook(target);nxt=pc+4
        elif op&0xff00 in (0x8900,0x8d00,0x8b00,0x8f00):
            if delay: raise ValueError('branch in delay slot')
            take=not self.t if op&0xff00 in (0x8b00,0x8f00) else self.t
            if op&0xff00 in (0x8d00,0x8f00):self.one(pc+2,True);nxt=pc+4
            if take:nxt=pc+4+signed(op&255,8)*2
        elif op>>12==0xa:
            if delay:raise ValueError('branch in delay slot')
            self.one(pc+2,True);nxt=pc+4+signed(op&0xfff,12)*2
        elif op&0xf0ff==0x0023:
            if delay:raise ValueError('branch in delay slot')
            target=self.r[n];self.one(pc+2,True);nxt=(pc+4+target)&0xffffffff
        elif op==0x000b:
            if delay: raise ValueError('branch in delay slot')
            self.one(pc+2,True);return 0
        else: raise ValueError(f'unsupported constructor opcode {op:04x} at {pc:08x}')
        return nxt

    def run(self):
        pc=self.pc
        while pc: pc=self.one(pc)
        result=[]
        for mod in self.modifiers:
            at=mod['payload']; size=mod['size']
            result.append({**mod,'bytes':bytes(self.read(at+i,1) for i in range(size)).hex()})
        return dict(kind=self.kind,instructions=self.step,modifiers=result,calls=self.calls,writes=self.writes,trace=self.trace)

    def dispatch(self):
        self.r=[0]*16;self.fr=[0]*16;self.pr=0;self.t=False
        self.r[5],self.r[6],self.r[7],self.r[15]=self.kind,0x22000000,0x22000100,0x30010000
        for at,values in ((self.r[6],(0.,0.,0.)),(self.r[7],(1.,2.,3.))):
            for i,v in enumerate(values):self.write(at+i*4,struct.unpack('<I',struct.pack('<f',v))[0],4)
        pc=0x8c06eae0
        while pc:pc=self.one(pc)
        if self.dispatched is None:raise ValueError(f'no native emitter callback kind{self.kind}')
        return self.dispatched.hex()

class Materials(Constructor):
    """Original c08b9c0 contexts; a resource-handle fixture never loads a texture."""
    def __init__(self,source):
        super().__init__(source,0,0x8c08b9c0);self.budget=10000
        self.write(0x8c0d0418,1,4) # The normalized-byte palette is already initialized; context creation is unchanged.

    def hook(self,target):
        if target==0x8c078a80:
            if self.r[4]!=0x0816e044:raise ValueError('Unexpected native VFX texture ID')
            self.write(0x22020004,1,4);self.r[0]=0x22020000 # GPU texture address does not enter blend enums.
        else:super().hook(target)

def parse_materials(source):
    material=Materials(source);pc=material.pc
    while pc:pc=material.one(pc)
    contexts=[]
    for kind in range(37):
        at=0x8c78c53c+kind*0x90
        words=[material.read(at+i*4,4) for i in range(36)]
        contexts.append(dict(kind=kind,context=hex(at),words=words,callback=hex(material.read(0x8c78c4a8+kind*4,4))))
    if any(c['words'][20]!=8 or c['words'][21] not in (6,10) for c in contexts):raise ValueError('Unrepresented native VFX blend enums')
    return contexts

def parse_effects(source):
    """Cold constructors plus native dispatcher, stopped before particle emission.

    The allocator hook matches fresh BSS, c220 marks slots free, c2a0 clears
    that byte, c520 fills the modifier callback/payload, c720 links the node.
    Payload addresses are fixtures. Their contents come from actual opcodes.
    """
    if hashlib.sha256(source).hexdigest() != SHA: raise ValueError('Unknown Dreamcast executable')
    read=lambda at,n:source[at-BASE:at-BASE+n]
    u=lambda at:struct.unpack('<I',read(at,4))[0]
    order=(0,1,2,3,4,5,6,9,7,8,10,13,11,12,14,18,16,17,19,15,20,21,22,23,24,25,26,27,28,29,30,31,32,33)
    at=0x8c06e200;definitions={};evidence=[]
    for kind in order:
        start=u(at);pool_at=at;at+=4 if kind in (0,1,2,3,4) else 8
        if kind not in SUPPORTED:continue
        runner=Constructor(source,kind,start);trace=runner.run()
        emitted=bytes.fromhex(runner.dispatch())
        mods={}
        for m in trace['modifiers']:mods.setdefault(m['type'],[]).append(bytes.fromhex(m['bytes']))
        one=lambda typ:mods.get(typ,[bytes(0x80)])[0]
        emit,sprite=one(25),one(34)
        floats=lambda b,off,n:list(struct.unpack_from('<'+str(n)+'f',b,off))
        scalar=lambda b,off:floats(b,off,1)[0]
        word=lambda b,off:struct.unpack_from('<I',b,off)[0]
        # Dispatcher fixture: origin=(0,0,0), endpoint=(1,2,3). The observed
        # displacement yields the source direction coefficient independently
        # of any PC definition. Selector+0xc==1 emits one regardless of range.
        velocity=floats(emit,0x30,3);dispatched=floats(emitted,0x30,3)
        direction=0
        if dispatched!=velocity:
            direction=dispatched[0]
            if any(abs(dispatched[i]-(i+1)*direction)>1e-6 for i in range(3)):
                raise ValueError('Native VFX direction does not fit EDVFX vector coefficient')
            velocity=[0.,0.,0.] # Native dispatcher replaces, rather than adds to, constructor velocity.
        count=struct.unpack_from('<i',emitted,0xc)[0]
        if count not in (-1,1):raise ValueError('Unrepresented native VFX count selector')
        counts=(1,1) if count==1 else struct.unpack_from('<2I',emitted,0x10)
        children=[{'flags':b[2],'kind':b[1],'chance':b[5]} for b in mods.get(31,[])]
        death=[c['kind'] for c in children if c['flags']==1]
        random=[c for c in children if c['flags']==4]
        periodic=[b for b in mods.get(31,[]) if b[2]==0x68]
        d=dict(kind=kind,constructor=start,cap=struct.unpack('<i',read(0x8c0c6e54+kind*4,4))[0],
            life=word(one(23),4),color=list(emit[0x1c:0x20]),delta=list(struct.unpack_from('<4b',emit,0x18)),
            velocity=velocity,acceleration=floats(one(24),4,3),size=scalar(emit,0x20),
            size_min=scalar(emit,0x24),size_max=scalar(emit,0x28),growth=scalar(emit,0x2c),
            uv=floats(sprite,0,4),columns=sprite[0x32],rows=sprite[0x33],random_frame=int(bool(sprite[0x10])),
            dispersion=floats(emit,0x3c,3),direction_scale=direction,count_min=counts[0],count_max=counts[1],
            frame_mode=sprite[0x12],frame_delay=struct.unpack_from('<H',sprite,0x22)[0],
            frame_initial=struct.unpack_from('<H',sprite,0x20)[0],collision=1 if 32 in mods else 2 if 1 in mods else 0,
            child_death=death[0] if death else 0,rotation_step=list(one(33)[1:4]),
            bounce_flags=one(1)[3] if 1 in mods else 0,bounce_percent=floats(one(1),0x10,3) if 1 in mods else [100]*3,
            random_child=0,random_child_chance=0,periodic_child=0,child_period=0,child_max_age=0,child_initial=0,
            required_mode=0,ring=[0]*6,ring_color=[0]*3,ring_segments=0,children=[])
        if periodic:
            b=periodic[0];d.update(periodic_child=b[1],child_period=b[3],child_max_age=struct.unpack_from('<H',b,6)[0],child_initial=b[4])
        if kind in (16,32):d['children']=children
        elif random:
            if len(random)!=1:raise ValueError('Multiple simple random children')
            d.update(random_child=random[0]['kind'],random_child_chance=random[0]['chance'])
        if kind==16:d.update(jitter_min_age=one(12)[1],position_jitter=floats(one(12),4,3))
        definitions[kind]=d
        evidence.append(dict(kind=kind,constructor=hex(start),constructor_pool=hex(pool_at),
            modifier_types=[m['type'] for m in trace['modifiers']],modifiers=trace['modifiers'],
            dispatch_emitter=emitted.hex(),constructor_instructions=trace['instructions'],
            opcode_trace_sha256=hashlib.sha256(json.dumps(runner.trace).encode()).hexdigest()))
    # Native pool cbae0 loops inclusively over the signed short 0x5db.
    pool=struct.unpack('<h',read(0x8c06bb12,2))[0]+1
    if pool!=1500 or set(definitions)!=SUPPORTED:raise ValueError('Unrepresented Dreamcast VFX pool/scope')
    ring_base=u(0x8c08e814);corona_base=u(0x8c08f200)
    def ring(mode):
        row=read(ring_base+mode*40,40);curve=list(struct.unpack_from('<6f',row));rgb=list(struct.unpack_from('<3I',row,24))
        if any(c>255 for c in rgb):raise ValueError('Native ring RGB exceeds EDVFX byte ABI')
        return dict(mode=mode,curve=curve,color=rgb,segments=row[38],native_row=row.hex())
    def corona(mode):
        row=read(corona_base+mode*28,28);rgb=list(struct.unpack_from('<3I',row,8))
        if any(c>255 for c in rgb):raise ValueError('Native corona RGB exceeds EDVFX byte ABI')
        compact=row[:8]+bytes(rgb)+b'\0'+row[20:]
        return dict(mode=mode,raw=compact.hex(),native_row=row.hex())
    rings={mode:ring(mode) for mode in (0,1,2,3,11,12)}
    coronas={mode:corona(mode) for mode in (1,4,6)}
    definitions[25].update(required_mode=3,ring=rings[3]['curve'],ring_color=rings[3]['color'],ring_segments=rings[3]['segments'])
    return dict(definitions=definitions,rings=rings,coronas=coronas,pool=pool,evidence=evidence,materials=parse_materials(source),
        ring_table=hex(ring_base),corona_table=hex(corona_base))

def encode_files(parsed):
    # Only the format encoders are reused. PC executable parsers, PC scripts,
    # and PC-derived state goldens are never invoked by the Dreamcast exporter.
    from ed_vfx import encode_effects
    from ed_vfx_hits import encode_hit_effects
    from ed_vfx_emitters import encode_emitter_effects
    from ed_vfx_ranged import encode_ranged_effects
    from ed_vfx_tree import encode_tree_effects
    from ed_vfx_ash import encode_ash_effects
    from ed_vfx_variants import encode_variant_effects
    from ed_vfx_npc import encode_npc_effects
    d,r,c=parsed['definitions'],parsed['rings'],parsed['coronas']
    select=lambda *k:[d[i] for i in k]
    tree=dict(definitions=select(15,24),rings=[dict(mode=i,ring=r[i]['curve'],ring_color=r[i]['color'],ring_segments=r[i]['segments']) for i in (0,2)],corona_mode=4,corona_row=c[4]['raw'])
    variant=dict(definitions=select(3,32),ring=r[1],script_rings=[r[11],r[12]],corona=c[1],script_corona=c[6])
    # Existing native-mask packer analysis c031960/c0349e0 establishes native
    # enum8=SRC_ALPHA, enum6=INV_SRC_ALPHA, enum10=ONE. Preserve the raw enums
    # in a source-pinned optional extension; old profiles retain their blend ABI.
    render=bytearray(struct.pack('<8s32sI',b'EDVFXD1\0',bytes.fromhex(SHA),len(d)))
    for m in parsed['materials']:
        if m['kind'] in d:render.extend(struct.pack('<4I',m['kind'],m['words'][20],m['words'][21],int(m['callback'],16)))
    return {'render-states.bin':bytes(render),'effects.bin':encode_effects(select(9,23)),'hit-effects.bin':encode_hit_effects(select(2,18,19)),
        'emitter-effects.bin':encode_emitter_effects(select(0,25)),'ranged-effects.bin':encode_ranged_effects(select(7,22)),
        'tree-effects.bin':encode_tree_effects(tree),'ash-effects.bin':encode_ash_effects(select(20,8,6)),
        'variant-effects.bin':encode_variant_effects(variant),'npc-effects.bin':encode_npc_effects(select(17,21,16))}

def export_vfx(binary,atlas,output):
    binary,atlas,output=map(lambda p:Path(p).resolve(),(binary,atlas,output))
    checked_original_output(binary.parent,output)
    if output in binary.parents or output in atlas.parents or binary.parent in output.parents:
        raise ValueError('VFX output must be separate from the original input directory')
    source=binary.read_bytes();atlas_data=atlas.read_bytes()
    if hashlib.sha256(atlas_data).hexdigest()!=ATLAS_SHA:raise ValueError('Unknown original Dreamcast VFX atlas')
    parsed=parse_effects(source);files=encode_files(parsed)
    target=(output/'vfx').resolve()
    if output not in target.parents:raise ValueError('VFX output escapes generated assets')
    outputs={**files,'atlas.rgba':atlas_data}
    for name,data in outputs.items():
        at=(target/name).resolve()
        if target not in at.parents:raise ValueError('VFX file escapes generated assets')
        if at.exists() and at.read_bytes()!=data:raise ValueError('Refusing to overwrite differing VFX output: '+str(at))
    target.mkdir(parents=True,exist_ok=True)
    for name,data in outputs.items():(target/name).write_bytes(data)
    manifest=dict(source_sha256=SHA,atlas_sha256=ATLAS_SHA,atlas_id='0816e044',pool=parsed['pool'],
        scope='Original Dreamcast cold constructor and spawn-dispatch parameters for the 19 existing host effects; EDVFX ABI bridge',
        limitations=['Particle updates, RNG, collision arithmetic and mesh generation still use the existing host bridge; native runtime/render parity is not established.',
            'Native effect16 has one child9/chance5. Effect20 has death18 with no random child. Reinitialization from reused native payloads is not emulated.',
            'The remaining 15 registered constructors are outside the existing host effect scope; no PC definitions or state goldens are exported.'],
        definitions=list(parsed['definitions'].values()),native_evidence=parsed['evidence'],render_contexts=parsed['materials'],
        rings=list(parsed['rings'].values()),coronas=list(parsed['coronas'].values()),
        ring_table=parsed['ring_table'],corona_table=parsed['corona_table'],
        files={name:dict(size=len(data),sha256=hashlib.sha256(data).hexdigest()) for name,data in {**files,'atlas.rgba':atlas_data}.items()})
    (target/'source.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    return manifest

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True);parser.add_argument('--atlas',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();result=export_vfx(args.binary,args.atlas,args.output)
    print(json.dumps(dict(effects=sorted(d['kind'] for d in result['definitions']),files=result['files'])))

if __name__=='__main__':main()

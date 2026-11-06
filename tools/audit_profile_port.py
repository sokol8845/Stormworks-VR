"""Compare profiled native code without loading either EXE. HDE helper is local.

Normalized code preserves every opcode/register/field/constant. Only relative
operands are removed, then checked separately against their actual targets.
The report is evidence for review, not authorization to change an image hash.
"""
import argparse
import bisect
import ctypes as C
import hashlib
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
OBJECT_OFFSETS = {0xbb9f8, 0xce190, 0xce150, 0xce5c8, 0xce5f8, 0xce6b0, 0xcde80}
OWNER_FIELDS = {n:n+8 for n in (0xce568,0xce56c,0xce571,0xce578,0xce588,0xce590,0xce598,0xce5c8,0xce5f8,0xce6b0,0xce768,0xce780)}
GLOBAL_DISPLACEMENTS={n:n+16 for n in (0xceeee0,0xcf4ff0,0xcf4ff8,0xcf5008,0xcf5010,0xcf5084)}
VTABLE_SLOTS = {0xb75678:(0x38,0x2a8,0x440,0x450),0xbb1ed8:(0,8),0xb89568:(0,),0xb0fff0:(0,),0xb1a790:(0x20,)}
LEAVES = {0x95c22c: 5, 0x1413b0: 3, 0xcb690: 4, 0x37c4d0: 6, 0x35d4e0: 10}
SOURCES = [ROOT/'include/swvr/game_profile.hpp'] + [ROOT/'src'/name for name in (
    'gl_trace.cpp', 'interaction_probe.cpp', 'native_button_bindings.cpp',
    'native_button_retirement.cpp', 'native_hand_buttons.cpp', 'native_hand_ray.cpp', 'native_input_probe.cpp')]


class PE:
    def __init__(self, path):
        self.path = Path(path)
        self.data = self.path.read_bytes()
        nt = self.u32(0x3c)
        assert self.data[:2] == b'MZ' and self.data[nt:nt+4] == b'PE\0\0'
        optional = nt+24
        assert self.u16(optional) == 0x20b
        self.base = self.u64(optional+24)
        self.size = self.u32(optional+56)
        self.sections = []
        table = optional+self.u16(nt+20)
        for i in range(self.u16(nt+6)):
            at = table+40*i
            vs, rva, size, raw = struct.unpack_from('<IIII', self.data, at+8)
            self.sections.append((rva, max(vs, size), raw, size, self.data[at:at+8].rstrip(b'\0').decode()))
        exception, size = struct.unpack_from('<II', self.data, optional+112+3*8)
        raw = self.offset(exception)
        self.functions = [struct.unpack_from('<III', self.data, at)[:2] for at in range(raw, raw+size, 12)]
        self.starts = [a for a, _ in self.functions]
        self.by_start = dict(self.functions)
        self.imports = {}
        irva, isize = struct.unpack_from('<II', self.data,optional+112+8)
        if irva:
            for at in range(0,isize,20):
                original,_,_,name,iat=struct.unpack('<IIIII',self.read(irva+at,20))
                if not name: break
                dll=self.string(name)
                index=0
                while True:
                    value=int.from_bytes(self.read((original or iat)+index*8,8),'little')
                    if not value: break
                    symbol=f'ordinal:{value&65535}' if value&(1<<63) else self.string(value+2)
                    self.imports[iat+index*8]=dll+'!'+symbol;index+=1

    def u16(self, at): return struct.unpack_from('<H', self.data, at)[0]
    def u32(self, at): return struct.unpack_from('<I', self.data, at)[0]
    def u64(self, at): return struct.unpack_from('<Q', self.data, at)[0]
    def section(self, rva): return next((s for s in self.sections if s[0] <= rva < s[0]+s[1]), None)
    def rva(self, raw):
        section=next(s for s in self.sections if s[2]<=raw<s[2]+s[3])
        return section[0]+raw-section[2]
    def string(self,rva):
        at=self.offset(rva);return self.data[at:self.data.index(b'\0',at)].decode('ascii')
    def col(self,rva):
        sig,offset,cd,td,ch,self_rva=struct.unpack('<IIIIII',self.read(rva,24))
        if sig!=1 or self_rva!=rva: raise ValueError('Not an RTTI locator')
        return dict(offset=offset,cd=cd,type=td,hierarchy=ch,name=self.string(td+16))
    def offset(self, rva):
        section = self.section(rva)
        if section is None or rva-section[0] >= section[3]:
            raise ValueError(f'RVA {rva:x} has no file bytes')
        return section[2]+rva-section[0]
    def read(self, rva, size):
        section = self.section(rva)
        if section is None or rva-section[0]+size > section[1]: raise ValueError('Read beyond section')
        backed=max(0,min(size,section[3]-(rva-section[0])))
        at=section[2]+rva-section[0]
        return self.data[at:at+backed]+b'\0'*(size-backed)
    def function(self, rva):
        index = bisect.bisect_right(self.starts, rva)-1
        if index >= 0 and self.functions[index][0] <= rva < self.functions[index][1]: return self.functions[index]
        if rva in LEAVES: return rva, rva+LEAVES[rva]
        return None


class Reference(C.Structure):
    _fields_ = [('offset', C.c_uint32), ('length', C.c_uint32), ('delta', C.c_int64),
                ('kind', C.c_uint32), ('width', C.c_uint32), ('operand',C.c_uint32)]


class Decoder:
    def __init__(self, path):
        self.dll = C.CDLL(str(Path(path).resolve()))
        self.decode = self.dll.swvr_normalize64
        self.decode.argtypes = [C.c_void_p, C.c_uint, C.c_void_p, C.POINTER(Reference), C.c_uint, C.POINTER(C.c_uint)]
        self.decode.restype = C.c_uint
        self.instruction = self.dll.swvr_instruction64
        self.instruction.argtypes = [C.c_void_p]
        self.instruction.restype = C.c_uint
        self.cache = {}

    def normalize(self, pe, start, end):
        key = pe, start, end
        if key in self.cache: return self.cache[key]
        raw = pe.read(start, end-start)
        # MSVC .pdata extents include trailing switch destinations and byte
        # index tables. Decode instructions only; compare the data as data.
        code_size=len(raw)
        for offset in range((-start)%4,len(raw)-11,4):
            values=struct.unpack_from('<3I',raw,offset)
            if all(start<=v<start+offset for v in values):
                code_size=offset;break
        source = C.create_string_buffer(raw+b'\0'*16)
        output = C.create_string_buffer(code_size)
        refs = (Reference*(len(raw)+1))()
        count = C.c_uint()
        error = self.decode(source, code_size, output, refs, len(refs), C.byref(count))
        if error: raise ValueError(f'Decoder rejected {start+error-1:x}')
        references = [(r.offset,r.length,r.kind,r.width,
            r.delta if r.kind==3 else start+r.offset+r.length+r.delta,r.operand) for r in refs[:count.value]]
        tail=bytearray(raw[code_size:])
        for offset in range(0,len(tail)-3,4):
            value=struct.unpack_from('<I',tail,offset)[0]
            if not start<=value<start+code_size:continue
            struct.pack_into('<I',tail,offset,value-start)
            references.append((code_size+offset,4,4,4,value,code_size+offset))
        result = output.raw+tail, references
        self.cache[key] = result
        return result


class Audit:
    def __init__(self, old, new, decoder, reviewed_callees=()):
        self.old, self.new, self.decoder = old, new, decoder
        self.mapping = {}
        self.functions = {}
        self.errors = []
        self.references = []
        self.pairs = set()
        self.data_mapping = {}
        self.vtables = []
        self.field_migrations = set()
        self.reviewed_callees=reviewed_callees
        self.accepted_changes=[]

    def equivalent(self,a,ae,b,be):
        if ae-a!=be-b:return False
        na,ra=self.decoder.normalize(self.old,a,ae);nb,rb=self.decoder.normalize(self.new,b,be)
        if len(ra)!=len(rb):return False
        adjusted=bytearray(na);changes=[]
        for old,new in zip(ra,rb):
            if old[:4]!=new[:4] or old[5]!=new[5]:return False
            if old[2]==3 and old[4]!=new[4]:
                if a<=old[4]<ae and b<=new[4]<be and old[4]-a==new[4]-b:
                    # Image-relative local switch-table/index address.
                    pass
                elif GLOBAL_DISPLACEMENTS.get(old[4])==new[4]:
                    if self.old.read(old[4],4)!=self.new.read(new[4],4):return False
                elif a==0x7126e0 and old[0]==0xa0 and old[4]==0x712d18 and new[4]==0x712d48:
                    # Six image-relative switch destinations, not an object field.
                    av=struct.unpack('<6I',self.old.read(old[4],24))
                    bv=struct.unpack('<6I',self.new.read(new[4],24))
                    if any(x-a!=y-b for x,y in zip(av,bv)):return False
                elif OWNER_FIELDS.get(old[4])!=new[4]:return False
                adjusted[old[5]:old[5]+old[3]]=int(new[4]).to_bytes(old[3],'little',signed=True)
                changes.append((a,old[0],old[4],new[4]))
        if adjusted!=nb:return False
        self.field_migrations.update(changes)
        for old,new in zip(ra,rb):
            if old[2]==2 and self.old.section(old[4]) and self.old.section(old[4])[4]!='.text':
                previous=self.data_mapping.setdefault(old[4],new[4])
                if previous!=new[4]:raise ValueError('Conflicting data mapping')
        return True

    def bound(self, pe, address):
        bound = pe.function(address)
        if bound: return bound
        pending, visited, end = [address], set(), address
        while pending:
            at = pending.pop()
            while at not in visited:
                if not address <= at < address+0x1000: raise ValueError(f'Unbounded leaf {address:x}')
                visited.add(at)
                raw = pe.read(at,16)
                info = self.decoder.instruction(C.create_string_buffer(raw))
                length, op, op2, reg = info&255, (info>>8)&255, (info>>16)&255, info>>24
                if not length: raise ValueError(f'Leaf decoder rejected {at:x}')
                end = max(end,at+length)
                if op in (0xc2,0xc3,0xca,0xcb) or (op==0xff and reg==4): break
                branch = op in (0xe9,0xeb) or 0x70<=op<=0x7f or (op==0x0f and 0x80<=op2<=0x8f)
                if branch:
                    _, refs = self.decoder.normalize(pe,at,at+length)
                    target = refs[0][4]
                    if address <= target < address+0x1000 and target not in pe.by_start: pending.append(target)
                    if op in (0xe9,0xeb): break
                at += length
        return address,end

    def compare_code(self, old_at, new_at):
        for review in self.reviewed_callees:
            if old_at==int(review['old_rva'],16) and new_at==int(review['new_rva'],16):
                for pe,at,side in ((self.old,old_at,'old'),(self.new,new_at,'new')):
                    if hashlib.sha256(pe.read(at,review[side+'_size'])).hexdigest()!=review[side+'_sha256']:return False
                if review not in self.accepted_changes:self.accepted_changes.append(review)
                return True
        a, ae = self.bound(self.old,old_at)
        b, be = self.bound(self.new,new_at)
        if old_at-a != new_at-b or ae-a != be-b: return False
        if not self.equivalent(a,ae,b,be): return False
        self.pairs.add((a,b,ae-a))
        return True

    def map_code(self, address):
        if address in self.mapping: return self.mapping[address]
        old_fn = self.bound(self.old,address)
        start, end = old_fn
        if start not in self.functions:
            normalized, refs = self.decoder.normalize(self.old, start, end)
            candidates = []
            for delta in (0x30,0x80,0xb0,0xc0,0,-0x30):
                candidate = start+delta
                new_end = self.new.by_start.get(candidate)
                if start not in self.old.by_start:
                    try: _,new_end=self.bound(self.new,candidate)
                    except ValueError: continue
                if new_end != candidate+(end-start): continue
                try:
                    norm, new_refs = self.decoder.normalize(self.new, candidate, new_end)
                    if self.equivalent(start,end,candidate,new_end): candidates.append((candidate, new_refs))
                except ValueError: pass
            if not candidates:
                for candidate, new_end in self.new.functions:
                    if abs(candidate-start) > 0x2000 or new_end-candidate != end-start: continue
                    try:
                        norm, new_refs = self.decoder.normalize(self.new, candidate, new_end)
                        if self.equivalent(start,end,candidate,new_end): candidates.append((candidate, new_refs))
                    except ValueError: pass
            if len(candidates) != 1: raise ValueError(f'{start:x}: {len(candidates)} matching normalized functions')
            candidate, new_refs = candidates[0]
            self.functions[start] = dict(old_start=start, new_start=candidate, size=end-start,
                normalized_sha256=hashlib.sha256(normalized).hexdigest(), reference_count=len(refs))
            self.references.extend((start,end,candidate,old,new) for old,new in zip(refs,new_refs))
        result = self.functions[start]['new_start']+address-start
        self.mapping[address] = result
        return result

    def data_equal(self, old_at, new_at, size):
        old, new = self.old.read(old_at, size), self.new.read(new_at, size)
        for at in range(0, size, 8):
            a, b = old[at:at+8], new[at:at+8]
            if a == b: continue
            if len(a) != 8: return False
            va, vb = int.from_bytes(a, 'little'), int.from_bytes(b, 'little')
            if not (self.old.base <= va < self.old.base+self.old.size and self.new.base <= vb < self.new.base+self.new.size): return False
            ra, rb = va-self.old.base, vb-self.new.base
            if self.old.section(ra)[4] == '.text':
                if not self.compare_code(ra, rb): return False
            elif self.data_mapping.get(ra,ra) != rb: return False
        return True

    def map_vtable(self,rva):
        if rva in self.data_mapping and any(v['old']==rva for v in self.vtables):return self.data_mapping[rva]
        locator=int.from_bytes(self.old.read(rva-8,8),'little')-self.old.base
        old=self.old.col(locator);needle=old['name'].encode()+b'\0';candidates=[]
        cursor=self.new.data.find(needle)
        while cursor>=0:
            td=self.new.rva(cursor)-16;pattern=struct.pack('<I',td);at=self.new.data.find(pattern)
            while at>=0:
                try:
                    col_rva=self.new.rva(at)-12;col=self.new.col(col_rva)
                    if col['offset']==old['offset'] and col['name']==old['name']:
                        pointer=struct.pack('<Q',self.new.base+col_rva);slot=self.new.data.find(pointer)
                        while slot>=0:
                            table=self.new.rva(slot)+8
                            if table%8==0:candidates.append(table)
                            slot=self.new.data.find(pointer,slot+1)
                except (ValueError,StopIteration,UnicodeDecodeError):pass
                at=self.new.data.find(pattern,at+1)
            cursor=self.new.data.find(needle,cursor+1)
        candidates=sorted(set(candidates))
        if len(candidates)!=1:raise ValueError(f'Vtable {rva:x}: {len(candidates)} RTTI matches')
        target=candidates[0]
        for slot in VTABLE_SLOTS.get(rva,()):
            a=int.from_bytes(self.old.read(rva+slot,8),'little')-self.old.base
            b=int.from_bytes(self.new.read(target+slot,8),'little')-self.new.base
            if not self.compare_code(a,b):raise ValueError(f'Vtable method {rva:x}+{slot:x} changed')
            self.mapping[a]=b
        self.vtables.append(dict(old=rva,new=target,name=old['name'],checked_slots=VTABLE_SLOTS.get(rva,())))
        self.data_mapping[rva]=target
        return target

    def run(self, rvas):
        ordered=sorted(rvas,key=lambda r:(self.old.section(r) or (0,0,0,0,''))[4]!='.text')
        for rva in ordered:
            section = self.old.section(rva)
            if section is None: self.errors.append(f'Unclassified RVA {rva:x}'); continue
            try:
                if section[4] == '.text': self.map_code(rva)
                else:
                    if section[4]=='.rdata':self.mapping[rva]=self.map_vtable(rva)
                    else:
                        if rva not in self.data_mapping:raise ValueError(f'Data {rva:x} has no native reference proving its identity')
                        target=self.data_mapping[rva]
                        if not self.data_equal(rva,target,4):raise ValueError(f'Data {rva:x} changed')
                        self.mapping[rva]=target
            except ValueError as error: self.errors.append(str(error))
        # Check actual direct targets of profile functions. Callee equivalence
        # does not recursively claim equivalence of the entire game call graph.
        at = 0
        while at < len(self.references):
            start,end,target,old,new = self.references[at]; at += 1
            try:
                if old[:4] != new[:4]: raise ValueError('Reference shape differs')
                if old[2]==3:continue # Explicit field migrations checked by equivalent().
                a, b = old[4], new[4]
                if a==b==0:continue
                if start<=a<end:
                    if a-start!=b-target:raise ValueError('Internal destination changed')
                    continue
                sa, sb = self.old.section(a), self.new.section(b)
                if sa is None or sb is None or sa[4] != sb[4]: raise ValueError(f'Reference sections differ {a:x}/{b:x}')
                if sa[4] == '.text':
                    if old[2]==2:
                        # LEA of jump-table/base labels is not a function call.
                        if a-start!=b-target:raise ValueError(f'Code data label differs {a:x}/{b:x}')
                    elif not self.compare_code(a,b): raise ValueError(f'Code target differs {a:x}/{b:x}')
                elif sa[4]=='.rdata':
                    if a in self.old.imports and b in self.new.imports:
                        if self.old.imports[a]!=self.new.imports[b]:raise ValueError(f'Import differs {a:x}/{b:x}')
                    elif a in self.mapping and self.mapping[a]==b:pass
                    elif not self.data_equal(a,b,16):
                        try:
                            if self.map_vtable(a)!=b:raise ValueError('Vtable moved differently')
                        except (ValueError,UnicodeDecodeError,StopIteration):
                            try:
                                if self.old.col(a)['name']!=self.new.col(b)['name']:raise ValueError('RTTI differs')
                            except (ValueError,UnicodeDecodeError):
                                # String operands end at NUL; adjacent pooled data can move.
                                instruction=self.old.read(start+old[0],16)
                                info=self.decoder.instruction(C.create_string_buffer(instruction))
                                opcode,opcode2=(info>>8)&255,(info>>16)&255
                                width=1 if (opcode,opcode2)==(0x0f,0xb6) else 2 if (opcode,opcode2)==(0x0f,0xb7) else 4 if opcode==0x8b and not instruction[0]&0xf8==0x48 else 0
                                equal=(width and self.data_equal(a,b,width)) or (opcode==0x8d and self.old.string(a)==self.new.string(b))
                                if not equal:
                                    raise ValueError(f'Data reference differs {a:x}/{b:x}')
            except ValueError as error: self.errors.append(f'{start:x}+{old[0]:x} -> {old[4]:x}: {error}')
        return dict(old_sha256=hashlib.sha256(self.old.data).hexdigest(), new_sha256=hashlib.sha256(self.new.data).hexdigest(),
            game_launched=False, mapping={f'0x{k:x}': f'0x{v:x}' for k,v in sorted(self.mapping.items())},
            functions=list(self.functions.values()), direct_code_pairs=[list(p) for p in sorted(self.pairs)],vtables=self.vtables,
            field_migrations=[dict(function=a,instruction=o,old=x,new=y) for a,o,x,y in sorted(self.field_migrations)],
            checked_references=at, errors=sorted(set(self.errors)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--old', type=Path, default=ROOT/'artifacts/stormworks-f7d605e34fe3.exe')
    parser.add_argument('--new', type=Path, default=Path('C:/Program Files (x86)/Steam/steamapps/common/Stormworks/stormworks64.exe'))
    parser.add_argument('--decoder', type=Path, default=ROOT/'build/capture/Release/swvr_profile_decoder.dll')
    parser.add_argument('--output', type=Path, default=ROOT/'artifacts/profile-port-20261102.json')
    parser.add_argument('--spec',type=Path,default=ROOT/'tools/profile_port_26275574_to_26294447.json')
    args = parser.parse_args()
    spec=json.loads(args.spec.read_text())
    old,new=PE(args.old),PE(args.new)
    for pe,side in ((old,'old'),(new,'new')):
        if hashlib.sha256(pe.data).hexdigest()!=spec[side+'_sha256']:raise ValueError(f'{side} executable SHA-256 mismatch')
    audit=Audit(old,new,Decoder(args.decoder),spec['reviewed_callees'])
    result = audit.run(int(x,16) for x in spec['old_rvas'])
    result['reviewed_callee_changes']=audit.accepted_changes
    args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(dict(mapping_count=len(result['mapping']), functions=len(result['functions']),
        checked_references=result['checked_references'], error_count=len(result['errors']), errors=result['errors'][:45]), indent=2))
    return bool(result['errors'])


if __name__ == '__main__':
    raise SystemExit(main())

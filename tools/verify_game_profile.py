"""Check source signature guards against the installed PE without loading it."""
import argparse
import hashlib
import json
from pathlib import Path
import re

from audit_profile_port import PE, ROOT, SOURCES


def verify(executable):
    pe=PE(executable)
    header=(ROOT/'include/swvr/game_profile.hpp').read_text(encoding='utf-8')
    expected=re.search(r'sha256\[\]="([0-9a-f]{64})"',header)[1]
    actual=hashlib.sha256(pe.data).hexdigest()
    errors=[]
    checked=[]
    if actual!=expected:errors.append('Executable SHA-256 differs from game_profile.hpp')

    def check(source,name,rva,raw):
        checked.append(dict(source=str(source.relative_to(ROOT)),name=name,rva=hex(rva),size=len(raw)))
        if pe.read(rva,len(raw))!=raw:errors.append(f'{source.name}: {name} differs at {rva:x}')

    def raw_bytes(body):
        return bytes(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{1,2})\b',body))

    aliases={'serializer':'serializer','dispatch':'dispatch','raw':'raw_clear','typed':'typed_clear',
             'delete':'delete','free':'free','reset':0x23ff80,'destroy':0x23ea20}
    for source in SOURCES+[ROOT/'src/native_avatar_identity.cpp',ROOT/'src/native_locomotion.cpp',ROOT/'include/swvr/native_keypad_request.hpp',ROOT/'src/native_vehicle_keypad.cpp']:
        text=source.read_text(encoding='utf-8')
        values={name:int(value,16) for name,value in re.findall(r'\b(\w+)\s*=\s*(0x[0-9a-fA-F]+)',text)}
        for name,body in re.findall(r'std::array<unsigned char,\d+>\s+(\w+)\s*\{([^}]+)\}',text):
            if not name.endswith(('_prefix','_bytes')):continue
            stem=name.rsplit('_',1)[0]
            if source.name=='native_button_retirement.cpp':stem=aliases[stem]
            rva=stem if isinstance(stem,int) else values[stem+'_rva']
            check(source,name,rva,raw_bytes(body))
            if source.name=='native_avatar_identity.cpp' and name=='opaque_prefix':
                for producer in ('glass_rva','additive_rva','shadow_rva'):check(source,producer,rva=values[producer],raw=raw_bytes(body))
        if source.name=='native_avatar_identity.cpp':
            # The shadow palette binding relies on these exact submit CALLs.
            block=text.split('submit_calls{{',1)[1].split('}};',1)[0]
            for site,target in re.findall(r'\{(0x[0-9a-fA-F]+),(\w+)\}',block):
                site=int(site,16);target=values[target]
                instruction=pe.read(site,5)
                checked.append(dict(source=str(source.relative_to(ROOT)),name='avatar submit CALL',rva=hex(site),size=5))
                if instruction[0]!=0xe8 or site+5+int.from_bytes(instruction[1:],'little',signed=True)!=target:
                    errors.append(f'{source.name}: submit CALL at {site:x} does not reach {target:x}')
        if source.name=='native_hand_buttons.cpp':
            for rva,body in re.findall(r'\{(0x[0-9a-fA-F]+),\{([^}]+)\}\}',text):
                raw=raw_bytes(body)
                if len(raw)==24:check(source,'hook entry',int(rva,16),raw)
        if source.name=='native_locomotion.cpp':
            check(source,'full input serializer vtable',values['full_input_vtable'],(pe.base+values['full_input_serialize_rva']).to_bytes(8,'little'))
            for site in (0x86a366,0x86cde6):
                instruction=pe.read(site,5)
                checked.append(dict(source=str(source.relative_to(ROOT)),name='movement sender CALL',rva=hex(site),size=5))
                if instruction[0]!=0xe8 or site+5+int.from_bytes(instruction[1:],'little',signed=True)!=values['input_send_rva']:
                    errors.append('Movement sender CALL differs')
            checked.append(dict(source=str(source.relative_to(ROOT)),name='movement serializer vtable',rva=hex(values['request_vtable']),size=8))
            if int.from_bytes(pe.read(values['request_vtable'],8),'little')!=pe.base+values['serialize_rva']:
                errors.append('Movement serializer vtable differs')
        if source.name=='native_keypad_request.hpp':
            check(source,'keypad serializer vtable',values['keypad_request_vtable_rva'],(pe.base+values['keypad_serialize_rva']).to_bytes(8,'little'))
        if source.name=='native_vehicle_keypad.cpp':
            for rva,body in re.findall(r'\{(0x[0-9a-fA-F]+),\{([^}]+)\}\}',text):
                raw=raw_bytes(body)
                if len(raw)==4:check(source,'frontend ABI guard',int(rva,16),raw)
            call=pe.read(0x84d202,5)
            checked.append(dict(source=str(source.relative_to(ROOT)),name='frontend event CALL',rva='0x84d202',size=5))
            if call[0]!=0xe8 or 0x84d207+int.from_bytes(call[1:],'little',signed=True)!=0x7b6060:errors.append('Keypad frontend CALL differs')
        if source.name=='native_input_probe.cpp':
            block=text.split('call_sites{{',1)[1].split('}};',1)[0]
            for rva,body in re.findall(r'\{(0x[0-9a-fA-F]+),\{([^}]+)\}\}',block):
                check(source,'native CALL',int(rva,16)-5,raw_bytes(body))
    # The maintenance-frame guard depends on identity of this BSS global,
    # which cannot be established by comparing its zero-initialized bytes.
    counter=int(re.search(r'render_counter_rva=(0x[0-9a-f]+)',header)[1],16)
    for rva,opcode in ((0x6ddab5,b'\x44\x8b\x05'),(0x6ddad0,b'\x44\x89\x05')):
        instruction=pe.read(rva,7)
        target=rva+7+int.from_bytes(instruction[3:],'little',signed=True)
        checked.append(dict(source='game_profile.hpp',name='render counter reference',rva=hex(rva),size=7))
        if instruction[:3]!=opcode or target!=counter:errors.append('Render counter identity differs from native load/store')
    return dict(sha256=actual,game_launched=False,checked_guards=checked,errors=errors)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',nargs='?',type=Path,default=Path('C:/Program Files (x86)/Steam/steamapps/common/Stormworks/stormworks64.exe'))
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    report=verify(args.executable)
    if args.output:args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(sha256=report['sha256'],checked_guards=len(report['checked_guards']),errors=report['errors']),indent=2))
    return bool(report['errors'])


if __name__=='__main__':
    raise SystemExit(main())

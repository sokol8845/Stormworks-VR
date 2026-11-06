"""Read the survivor .anim skeleton tables; never load or modify a game process.

This is a bounded inspection of the installed asset format, not a runtime rig
binding. Transform semantics and palette ownership still need native validation.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct


def inspect(path):
    data=path.read_bytes()
    if data[:8]!=b'anim\x01\0\0\0':raise ValueError('Unsupported anim header/version')
    candidates=[]
    marker=b'\x04\0root'
    cursor=data.find(marker)
    while cursor>=4:
        try:
            count=struct.unpack_from('<I',data,cursor-4)[0]
            if not 1<=count<=64:raise ValueError('Skeleton count outside bounds')
            at=cursor;bones=[]
            for index in range(count):
                length=struct.unpack_from('<H',data,at)[0];at+=2
                if not 1<=length<=64:raise ValueError('Invalid node name length')
                name=data[at:at+length].decode('ascii');at+=length
                if not name.replace('_','').isalnum():raise ValueError('Invalid node name')
                transform=struct.unpack_from('<12f',data,at);at+=48
                if not all(math.isfinite(v) for v in transform):raise ValueError('Nonfinite node transform')
                parent,child_count=struct.unpack_from('<iI',data,at);at+=8
                if not -1<=parent<count or child_count>count:raise ValueError('Invalid node links')
                children=list(struct.unpack_from('<'+'I'*child_count,data,at));at+=child_count*4
                if any(c>=count for c in children) or len(set(children))!=len(children):raise ValueError('Invalid child index')
                bones.append(dict(index=index,name=name,parent=parent,children=children,raw_transform_12f=transform))
            if bones[0]['name']!='root' or bones[0]['parent']!=-1:raise ValueError('Invalid root')
            if len({b['name'] for b in bones})!=count:raise ValueError('Duplicate node name')
            for bone in bones[1:]:
                parent=bone['parent']
                if parent<0 or bone['index'] not in bones[parent]['children']:raise ValueError('Parent/child mismatch')
            for bone in bones:
                if any(bones[c]['parent']!=bone['index'] for c in bone['children']):raise ValueError('Child/parent mismatch')
                seen=set();node=bone['index']
                while node!=-1:
                    if node in seen:raise ValueError('Skeleton cycle')
                    seen.add(node);node=bones[node]['parent']
            required={'head','chest','stomach','left_shoulder','left_elbow','left_hand','right_shoulder','right_elbow','right_hand'}
            if not required<={b['name'] for b in bones}:raise ValueError('Missing survivor nodes')
            candidates.append(dict(table_offset=hex(cursor-4),table_end=hex(at),bone_count=count,bones=bones))
        except (ValueError,UnicodeDecodeError,struct.error):pass
        cursor=data.find(marker,cursor+1)
    if len(candidates)!=1:raise ValueError(f'Expected one validated skeleton table; found {len(candidates)}')
    return dict(asset=str(path),sha256=hashlib.sha256(data).hexdigest(),**candidates[0])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-directory',type=Path,default=Path('C:/Program Files (x86)/Steam/steamapps/common/Stormworks'))
    parser.add_argument('--output',type=Path,default=Path('artifacts/avatar-rigs-20261102.json'))
    args=parser.parse_args()
    report=dict(game_launched=False,runtime_binding_verified=False,assets=[inspect(args.game_directory/'rom/meshes'/f'survivor_{variant}.anim') for variant in ('male','female')])
    args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    for asset in report['assets']:
        print(Path(asset['asset']).name,asset['bone_count'],[(b['index'],b['name'],b['parent']) for b in asset['bones']])


if __name__=='__main__':
    main()

"""Check the FIRST native eye-source size; later resizing cannot hide bad startup."""
import argparse
import json
from pathlib import Path
import re


def verify(path,width=3440,height=1440):
    text=Path(path).read_text(encoding='utf-8')
    sizes=[tuple(map(int,m)) for m in re.findall(r'^#native_eye_source_config\s+size=(\d+)x(\d+)\b',text,re.M)]
    first=sizes[0] if sizes else None
    return dict(expected=[width,height],first_source=list(first) if first else None,
                later_sources=[list(s) for s in sizes[1:]],passed=first==(width,height))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture',type=Path)
    parser.add_argument('--width',type=int,default=3440)
    parser.add_argument('--height',type=int,default=1440)
    args=parser.parse_args();result=verify(args.capture,args.width,args.height)
    print(json.dumps(result,indent=2));raise SystemExit(not result['passed'])

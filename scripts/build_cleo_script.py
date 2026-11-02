"""Compile own headerless script only; never discover/install into a GTA folder."""
import argparse
import pathlib
import subprocess
p=argparse.ArgumentParser()
for name in ('compiler','source','output','header'):p.add_argument('--'+name,required=True)
a=p.parse_args()
out=pathlib.Path(a.output).resolve();out.parent.mkdir(parents=True,exist_ok=True)
subprocess.run([a.compiler,'--no-splash','--mode','sa','--compile',a.source,str(out),
                '--option','Compiler::AddExtraInfo','0'],check=True,timeout=30)
b=out.read_bytes()
needle=bytes.fromhex('017a030000030100')
assert len(b)<512 and b.count(needle)==1, 'Unexpected opcode/operand image'
pathlib.Path(a.header).write_text('#pragma once\n#include <array>\ninline constexpr std::array<unsigned char,'+
    str(len(b))+'> bootstrapScript{'+','.join(str(x) for x in b)+'};\ninline constexpr unsigned bootstrapOpcodeOffset='+
    str(b.index(needle))+';\n',encoding='ascii')

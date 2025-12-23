gen = [
  decodetree.process('insn.decode', extra_args: [ '--varinsnwidth', '64' ])
]

rl78_ss = ss.source_set()
rl78_ss.add(gen)
rl78_ss.add(files(
  'translate.c',
  'op_helper.c',
  'helper.c',
  'cpu.c',
  'gdbstub.c',
  'disas.c'
))

target_arch += {'rl78': rl78_ss}
target_system_arch += {'rl78': ss.source_set()}

rl78_ss = ss.source_set()
rl78_ss.add(files(
    'cpu.c',
    'decode.c',
    'disas.c',
    'translate.c',
    'helper.c',
    'op_helper.c',
    'gdbstub.c',
))

target_arch += { 'rl78': rl78_ss }
target_system_arch += { 'rl78': ss.source_set() }

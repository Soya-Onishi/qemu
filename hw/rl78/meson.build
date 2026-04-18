rl78_ss = ss.source_set()

rl78_ss.add(files(
    'rl78.c',
    'virt.c',
    'qtest.c',
    'boot.c', 
    'clock.c',
    'sau.c',
    'tau.c',
    'adc.c',
    'irq.c',
    'gpio.c',
    'intercomm.c',
))

hw_arch += { 'rl78': rl78_ss }

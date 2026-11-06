create_clock -name master -period 10 [get_ports a]
create_generated_clock -name derived -master_clock [get_clocks mast*] -source [get_ports a] -divide_by 2 [get_ports q]
set_clock_groups -asynchronous -group master -group derived

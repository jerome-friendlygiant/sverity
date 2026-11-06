create_clock -name master -period 10 [get_ports a]
create_generated_clock -name derived -source [get_pins u/clk] -divide_by 2 [get_pins u/q]
set_clock_groups -asynchronous -group master -group derived

create_clock -name clka -period 10 [get_ports clka]
create_generated_clock -name clkdiv -source [get_ports clka] -divide_by 2 [get_pins top.div]
set_clock_groups -logically_exclusive -group [get_clocks clka] -group [get_clocks clkdiv]

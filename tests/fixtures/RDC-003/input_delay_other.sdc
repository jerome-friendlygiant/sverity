create_clock -name clka -period 10 [get_ports clka]
create_clock -name clkb -period 13 [get_ports clkb]
set_clock_groups -asynchronous -group [get_clocks clka] -group [get_clocks clkb]
set_input_delay 2.0 -clock clkb [get_ports por_n]

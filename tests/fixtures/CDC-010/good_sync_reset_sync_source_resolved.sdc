create_clock -name clka -period 10 [get_ports clka]
create_clock -name clkb -period 7 [get_ports clkb]
set_clock_groups -asynchronous -group {clka} -group {clkb}
set_input_delay -clock clkb 1.0 [get_ports rst_b]

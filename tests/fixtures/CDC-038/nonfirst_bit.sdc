create_clock -name clka -period 10 [get_ports clka]
create_clock -name clkb -period 7 [get_ports clkb]
set_clock_groups -asynchronous -group {clka} -group {clkb}
set_false_path -from [get_cells {a_q[1]}] -to [get_cells {s1[1]}]

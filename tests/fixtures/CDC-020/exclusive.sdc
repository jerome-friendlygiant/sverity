create_clock -name clka -period 10 [get_ports clka]
create_clock -name clkb -period 7 [get_ports clkb]
set_clock_groups -logically_exclusive -group {clka} -group {clkb}

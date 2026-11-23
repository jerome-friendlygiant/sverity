create_clock -name clka [get_ports clka]
create_clock -name clkb [get_ports clkb]
set_clock_groups -asynchronous -group {clka} -group {clkb}

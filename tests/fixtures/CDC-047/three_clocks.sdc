create_clock -name clka -period 10 [get_ports clka]
create_clock -name clkb -period 7 [get_ports clkb]
create_clock -name clkc -period 5 [get_ports clkc]
set_clock_groups -asynchronous -group {clka} -group {clkb} -group {clkc}

create_clock -name clka -period 10 [get_ports clka]
create_clock -name clkb -period 7 [get_ports clkb]
create_clock -name clkc -period 5 [get_ports clkc]
set_clock_groups -asynchronous -group {clka} -group {clkb}
set_clock_groups -logically_exclusive -group {clkc} -group {clkb}
set_input_delay 1 -clock clka [get_ports {d[0] d[1]}]
set_input_delay 1 -clock clkc [get_ports {d[2]}]

set_units -time ns
create_clock -name shared -period 10 [get_ports {clka clkb}]
create_clock -add -name other -period 1 [get_ports clka]
set_clock_groups -asynchronous -group {shared} -group {other}
set_max_delay 1 -datapath_only -from [get_pins gray*] -to [get_pins s1*]

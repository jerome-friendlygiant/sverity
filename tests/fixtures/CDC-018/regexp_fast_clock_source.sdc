set_units -time ns
create_clock -name clka -period 10 [get_ports clka]
create_clock -name clkb -period 7 [get_ports clkb]
create_clock -add -name fast -period 2 [get_ports -regexp {^clka$}]
set_max_delay 5 -datapath_only -from [get_pins gray*] -to [get_pins s1*]

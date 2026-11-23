create_clock -name clka -period 10 [get_ports clka]
create_clock -name clkb -period 7 [get_ports clkb]
set_units -time ps
set_max_delay 5000 -datapath_only -from [get_pins gray*] -to [get_pins s1*]

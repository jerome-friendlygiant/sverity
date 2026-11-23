set_units -time ns
create_clock -name clka -period 10 [get_ports clka]
create_clock -name clkb -period 7 [get_ports clkb]
set_max_delay 5 -datapath_only -from [get_pins {gray[0] gray[1] gray[2] gray[3]}] -to [get_pins {s1[0] s1[1] s1[2] s1[3]}]

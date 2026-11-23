set_units -time ns
create_clock -name clka -period 10 [get_ports clka]
create_clock -name clkb -period 7 [get_ports clkb]
set_max_delay 5 -datapath_only -from [get_pins gray*] -to [get_pins s1*]
set_false_path -from [get_cells -hierarchical -filter {IS_SEQUENTIAL}] -to [get_cells -hierarchical -filter {IS_SEQUENTIAL}]

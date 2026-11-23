set_units -time ns
create_clock -name clka -period 10 [get_ports clka]
create_clock -name clkb -period 7 [get_ports clkb]
set_max_delay 5 -datapath_only -from [get_pins u_src/gray*] -to [get_pins u_dst/s1*]
set_false_path -from [get_cells u_src] -to [get_cells u_dst]

create_clock -name first -period 20 [get_ports rst_n]
create_clock -add -name second -period 21 [get_ports rst_n]
set_false_path -from [get_clocks second]

create_clock -name initial -period 10 [get_ports {rst_n[*]}]
create_clock -name current -period 10 [get_ports {rst_n[3]}]
set_false_path -from [get_clocks initial]

create_clock -name source -period 2 [get_ports src_clk]
create_clock -name destination -period 23 [get_ports dst_clk]
set_clock_groups -asynchronous -group [get_clocks source] -group [get_clocks destination]

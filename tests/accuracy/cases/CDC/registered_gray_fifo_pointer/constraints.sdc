create_clock -name source -period 10 [get_ports src_clk]
create_clock -name destination -period 13 [get_ports dst_clk]
set_clock_groups -asynchronous -group [get_clocks source] -group [get_clocks destination]
set_bus_skew 5 -from [get_registers gray_src*] -to [get_registers gray_meta*]

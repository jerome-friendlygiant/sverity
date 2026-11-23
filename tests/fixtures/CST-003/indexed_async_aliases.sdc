create_clock -name clk_a -period 10 [get_ports {clk[0]}]
create_clock -name clk_b -period 7 -add [get_ports {clk[0]}]
set_clock_groups -asynchronous -group {clk_a} -group {clk_b}

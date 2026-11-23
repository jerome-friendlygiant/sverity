create_clock -name launch -period 4 [get_ports launch_clk]
create_clock -name sample -period 20 [get_ports sample_clk]
set_clock_groups -asynchronous -group launch -group sample

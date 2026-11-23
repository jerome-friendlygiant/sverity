create_clock -name clk3 -period 10 [get_ports {clk[3]}]
create_generated_clock -name out_clk -source [get_ports {clk[0]}] -divide_by 1 [get_nets out_clk]

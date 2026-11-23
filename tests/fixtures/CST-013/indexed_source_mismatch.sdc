create_clock -name clk2 -period 10 [get_ports {clk[2]}]
create_clock -name clk3 -period 10 [get_ports {clk[3]}]
create_generated_clock -name out_clk -source [get_ports {clk[2]}] -divide_by 1 [get_nets out_clk]

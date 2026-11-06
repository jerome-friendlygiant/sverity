create_clock -name ref -period 5 [get_ports ref_clk]
create_generated_clock -name divided -source [get_ports ref_clk] -divide_by 8 [get_nets divided_clk]

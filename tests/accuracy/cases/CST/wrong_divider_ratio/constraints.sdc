create_clock -name ref_clk -period 10.000 [get_ports ref_clk]
create_generated_clock -name divided_clk -source [get_ports ref_clk] -divide_by 4 [get_ports divided_clk]

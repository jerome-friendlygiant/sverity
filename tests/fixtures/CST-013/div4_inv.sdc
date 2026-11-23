create_clock -name clk_src -period 10 [get_ports clk_src]
create_generated_clock -name clk_div4_n -source [get_ports clk_src] -divide_by 4 -invert [get_nets div4_n]

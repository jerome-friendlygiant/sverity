create_clock -name clk_src -period 10 [get_ports clk_src]
create_generated_clock -name clk_div2 -source [get_ports clk_src] -divide_by 2 [get_nets div2_q]

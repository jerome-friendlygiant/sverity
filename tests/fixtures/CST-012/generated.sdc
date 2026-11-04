create_clock -name clk_a -period 10 [get_ports clk_a]
create_generated_clock -name gen -source [get_ports clk_a] -divide_by 2 [get_ports clk_b]
set_output_delay -clock gen 1 [get_ports q]

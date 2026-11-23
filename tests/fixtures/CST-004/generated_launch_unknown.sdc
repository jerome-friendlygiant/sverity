create_clock -name clk -period 10 [get_ports clk]
create_generated_clock -name gclk -source [get_ports clk] -divide_by 2 [get_nets div_q]
set_input_delay -clock gclk 0 [get_ports e]

create_clock -name clk_a -period 10 [get_ports clk_a]
set_input_delay -clock clk_a -max 2 -min 0 [get_ports run_i]

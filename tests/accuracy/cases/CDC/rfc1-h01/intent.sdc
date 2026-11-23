set_units -time ns
create_clock -name clk_sys -period 10.0 [get_ports clk_sys]
create_generated_clock -name clk_div4 -source [get_ports clk_sys] -divide_by 4 [get_cells div2_clk_q]
set_input_delay -clock clk_div4 2.0 [get_ports {d_i}]
set_output_delay -clock clk_div4 1.0 [get_ports {q_o}]

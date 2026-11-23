set_units -time ns
create_clock -name clk_sys -period 10.0 [get_ports clk_sys]
set_input_delay -clock clk_sys 2.0 [get_ports {sync_rst_n d_i}]
set_output_delay -clock clk_sys 1.0 [get_ports {q_o}]

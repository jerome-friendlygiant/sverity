create_clock -name core -period 8 [get_ports clk]
set_input_delay -clock core 1 [get_ports {bank_a* bank_b* reset_n}]
set_output_delay -clock core 1 [get_ports output_q*]
set_case_analysis 0 [get_nets bank_sel]

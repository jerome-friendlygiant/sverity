set_units -time ns
create_clock -name clk_fast -period 4.0 [get_ports clk_fast]
create_clock -name clk_slow -period 10.0 [get_ports clk_slow]
set_clock_groups -physically_exclusive -group {clk_fast} -group {clk_slow}
set_input_delay -clock clk_fast 2.0 [get_ports {d_i}]
set_input_delay -clock clk_slow 2.0 [get_ports {slow_rst_ni}]
set_output_delay -clock clk_slow 1.0 [get_ports {q_o}]

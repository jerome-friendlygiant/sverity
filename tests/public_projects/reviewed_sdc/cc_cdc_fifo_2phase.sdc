# Reviewed external-boundary contract for common_cells cc_cdc_fifo_2phase.
# Two asynchronous clocks; every primary input and output is a synchronous port of
# src_clk_i or dst_clk_i (resets included), except that the dst_data_o output carries no
# output delay in this reviewed contract; only dst_valid_o is constrained.
set_units -time ns
create_clock -name src_clk_i -period 10 [get_ports src_clk_i]
create_clock -name dst_clk_i -period 7 [get_ports dst_clk_i]
set_clock_groups -asynchronous -group {src_clk_i} -group {dst_clk_i}
set_input_delay -clock src_clk_i 1.0 [get_ports {src_rst_ni src_data_i src_valid_i}]
set_input_delay -clock dst_clk_i 1.0 [get_ports {dst_rst_ni dst_ready_i}]
set_output_delay -clock src_clk_i 1.0 [get_ports {src_ready_o}]
set_output_delay -clock dst_clk_i 1.0 [get_ports {dst_valid_o}]

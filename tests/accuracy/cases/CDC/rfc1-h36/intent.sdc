set_units -time ns
create_clock -name clk_src -period 20.0 [get_ports clk_src]
create_clock -name clk_dst -period 8.0 [get_ports clk_dst]
set_clock_groups -asynchronous -group {clk_src} -group {clk_dst}
set_input_delay -clock clk_src 2.0 [get_ports {flag_i}]
set_input_delay -clock clk_dst 2.0 [get_ports {dst_rst_ni}]
set_output_delay -clock clk_dst 1.0 [get_ports {flag_o}]
set_clock_latency -source 0.5 [get_clocks clk_dst]

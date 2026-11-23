create_clock -name src -period 10 [get_ports clk_src]
create_clock -name dst -period 12 [get_ports clk_dst]
set_clock_groups -asynchronous -group src -group dst
set_input_delay -clock src 1 [get_ports event_in]
set_output_delay -clock dst 1 [get_ports event_out]

create_clock -name src -period 10 [get_ports clk_src]
create_clock -name dst -period 12 [get_ports clk_dst]
set_clock_groups -asynchronous -group src -group dst
set_input_delay -clock src 1 [get_ports {a b}]
set_output_delay -clock dst 1 [get_ports {xor_out packed_word repeated padded out_a out_b opaque_mix}]

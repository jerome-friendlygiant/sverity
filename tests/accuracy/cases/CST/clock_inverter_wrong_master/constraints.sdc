create_clock -name sample -period 6 [get_ports sample_ref]
create_clock -name debug -period 40 [get_ports debug_ref]
create_generated_clock -name sample_inverted -source [get_ports debug_ref] -divide_by 1 -invert [get_nets sample_clk]

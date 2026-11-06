create_clock -name source -period 10 [get_ports src_clk]
create_generated_clock -name destination -source [get_ports src_clk] -divide_by 2 [get_ports dst_clk]

create_clock -name ref_clk -period 10.000 [get_ports ref_clk]
# The divided clock feeds a sequential output in the reduced block.

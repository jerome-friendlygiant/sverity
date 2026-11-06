create_clock -name reference -period 20 [get_ports clk]
create_generated_clock -name fast -source [get_ports clk] -multiply_by 4 [get_pins u_pll/fast_clk]
set_input_delay -clock fast 1 [get_ports din*]
set_output_delay -clock fast 1 [get_ports dout*]

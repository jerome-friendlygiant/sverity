create_clock -name chk_clk -period 9.000 [get_ports clk_chk_i]
create_clock -name ref_clk -period 10.000 [get_ports clk_ref_i]
set_clock_groups -asynchronous -group {chk_clk} -group {ref_clk}
set_input_delay  -clock ref_clk -min 1.000 [get_ports en_i]
set_input_delay  -clock ref_clk -max 3.000 [get_ports en_i]
set_output_delay -clock ref_clk -min 0.500 [get_ports timeout_o]
set_output_delay -clock ref_clk -max 2.000 [get_ports timeout_o]

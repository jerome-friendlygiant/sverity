create_clock -name root_clk -period 10.000 [get_ports clk_i]
create_generated_clock -name div_clk -source [get_ports clk_i] -divide_by 4 [get_ports clk_div_o]

set_input_delay  -clock div_clk -min 1.000 [get_ports data_i]
set_input_delay  -clock div_clk -max 3.000 [get_ports data_i]
set_input_delay  -clock div_clk -min 1.000 [get_ports en_i]
set_input_delay  -clock div_clk -max 3.000 [get_ports en_i]
set_output_delay -clock div_clk -min 0.500 [get_ports data_o]
set_output_delay -clock div_clk -max 2.500 [get_ports data_o]

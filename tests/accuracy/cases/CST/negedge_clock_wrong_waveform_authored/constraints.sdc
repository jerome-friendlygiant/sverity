create_clock -name ddr_clk -period 8.000 -waveform {0.000 4.000} [get_ports clk_i]
set_input_delay 1.000 -clock ddr_clk [get_ports {valid_i data_i}]
set_output_delay 1.000 -clock ddr_clk [get_ports {valid_o data_o}]
set_false_path -from [get_ports rst_ni]

create_clock -name clk -period 10 [get_ports clk]
set_input_delay 2 -clock clk [get_ports {rst s_valid s_data m_ready}]
set_output_delay 2 -clock clk [get_ports {s_ready m_valid m_data}]

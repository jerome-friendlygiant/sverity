create_clock -name cfg -period 10 [get_ports clk_cfg]
create_clock -name dma -period 6 [get_ports clk_dma]
set_clock_groups -asynchronous -group [get_clocks cfg] -group [get_clocks dma]
set_input_delay -clock cfg -max 2 -min 0 [get_ports sw_rst_req_i]

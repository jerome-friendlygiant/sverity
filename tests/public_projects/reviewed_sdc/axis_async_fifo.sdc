# Reviewed external-boundary contract for verilog-axis axis_async_fifo (default parameters).
# Two asynchronous clocks; every primary input and output is a synchronous port of s_clk or
# m_clk (resets included). The write and read Gray pointers cross through set_max_delay
# -datapath_only bounds below the faster clock period, which keeps multi-bit Gray skew under
# one capture period.
set_units -time ns
create_clock -name s_clk -period 10 [get_ports s_clk]
create_clock -name m_clk -period 7 [get_ports m_clk]
set_clock_groups -asynchronous -group {s_clk} -group {m_clk}
set_input_delay -clock s_clk 1.0 [get_ports {s_rst s_axis_tdata s_axis_tkeep s_axis_tvalid s_axis_tlast s_axis_tid s_axis_tdest s_axis_tuser s_pause_req}]
set_input_delay -clock m_clk 1.0 [get_ports {m_rst m_axis_tready m_pause_req}]
set_output_delay -clock s_clk 1.0 [get_ports {s_axis_tready s_status_depth s_status_depth_commit s_status_overflow s_status_bad_frame s_status_good_frame}]
set_output_delay -clock m_clk 1.0 [get_ports {m_axis_tdata m_axis_tvalid m_axis_tlast m_axis_tuser m_status_depth m_status_depth_commit m_status_overflow m_status_bad_frame m_status_good_frame}]
set_max_delay -datapath_only 6.5 -from [get_cells wr_ptr_gray_reg*] -to [get_cells wr_ptr_gray_sync1_reg*]
set_max_delay -datapath_only 6.5 -from [get_cells rd_ptr_gray_reg*] -to [get_cells rd_ptr_gray_sync1_reg*]

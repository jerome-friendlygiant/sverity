create_clock -name s_clk -period 10 [get_ports s_clk]
create_clock -name m_clk -period 7 [get_ports m_clk]
set_clock_groups -asynchronous -group {s_clk} -group {m_clk}

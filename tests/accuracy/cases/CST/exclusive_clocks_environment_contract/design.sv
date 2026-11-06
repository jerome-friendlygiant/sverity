module top(input logic src_clk,
  dst_clk,
  select_clock,
  
           input logic [7:0] din,
  output logic [7:0] dout);

  wire active_clock;
  // The system controller supplies select_clock; its protocol is external.
  assign active_clock = select_clock ? dst_clk : src_clk;
  always_ff @(posedge active_clock) begin
    dout <= din;
  end

endmodule

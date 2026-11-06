module top(input logic src_clk,
  dst_clk,
  output logic [3:0] a,
  b);

  // Two independent free-running diagnostic counters.
  initial begin a = 0; b = 0; end
  always @(posedge src_clk) begin
    a <= a + 1'b1;
  end
  always @(posedge dst_clk) begin
    b <= b + 1'b1;
  end

endmodule

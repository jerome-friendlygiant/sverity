module top(input logic src_clk,
  dst_clk,
  value,
  output logic received);

  logic launched;
  always_ff @(posedge src_clk) begin
    launched <= value;
  end
  always_ff @(posedge dst_clk) begin
    received <= launched;
  end

endmodule

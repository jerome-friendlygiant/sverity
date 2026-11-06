module top(input logic src_clk,
  dst_clk,
  input logic value,
  output logic result);

  logic launch;
  always_ff @(posedge src_clk) begin
    launch <= value;
  end
  always_ff @(posedge dst_clk) begin
    result <= launch;
  end

endmodule

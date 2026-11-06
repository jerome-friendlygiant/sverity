module top (
  input  logic clk_src_i,
  input  logic clk_dst_i,
  input  logic reset_ni,
  input  logic valid_src_i,
  input  logic [7:0] data_dst_i,
  output logic [7:0] data_dst_o
);
  logic valid_src_q;
  logic [7:0] data_q;
  logic       dst_reset_ni;

  always_ff @(posedge clk_src_i or negedge reset_ni) begin
    if (!reset_ni) valid_src_q <= 1'b0;
    else valid_src_q <= valid_src_i;
  end

  // A producer status bit is being treated as a reset in the destination.
  assign dst_reset_ni = valid_src_q;

  always_ff @(posedge clk_dst_i or negedge dst_reset_ni) begin
    if (!dst_reset_ni) data_q <= 8'h00;
    else data_q <= data_dst_i;
  end

  assign data_dst_o = data_q;
endmodule

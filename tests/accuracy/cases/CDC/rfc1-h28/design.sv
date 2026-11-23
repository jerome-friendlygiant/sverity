// Mode level input sampled into the consumer clock domain.
module top (
  input  logic src_clk,
  input  logic dst_clk,
  input  logic dst_rst_ni,
  input  logic level_i,
  output logic level_o
);
  logic l1_q;
  logic l2_q;
  logic dst_hold_q;

  always_ff @(posedge dst_clk) begin
    l1_q <= level_i;
    l2_q <= l1_q;
  end

  always_ff @(posedge dst_clk) begin
    if (!dst_rst_ni) dst_hold_q <= 1'b0;
    else             dst_hold_q <= l2_q;
  end

  assign level_o = dst_hold_q;
endmodule

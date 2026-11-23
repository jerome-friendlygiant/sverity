module sync2 (
  input  logic clk,
  input  logic d_i,
  output logic q_o
);
  logic s1_q;
  logic s2_q;

  always_ff @(posedge clk) begin
    s1_q <= d_i;
    s2_q <= s1_q;
  end

  assign q_o = s2_q;
endmodule

module top (
  input  logic src_clk,
  input  logic dst_clk,
  input  logic dst_rst_ni,
  input  logic fault_in_i,
  output logic fault_o
);
  logic sticky_q;
  logic sync_s;
  logic out_q;

  always_ff @(posedge src_clk) sticky_q <= sticky_q | fault_in_i;

  sync2 u_sync (.clk(dst_clk), .d_i(sticky_q), .q_o(sync_s));

  always_ff @(posedge dst_clk) begin
    if (!dst_rst_ni) out_q <= 1'b0;
    else             out_q <= sync_s;
  end

  assign fault_o = out_q;
endmodule

// Two status bits combined in the consumer domain.
module top (
  input  logic src_clk,
  input  logic dst_clk,
  input  logic a_i,
  input  logic b_i,
  output logic both_o
);
  logic a_src_q;
  logic b_src_q;
  logic a_s1_q;
  logic a_s2_q;
  logic b_s1_q;
  logic b_s2_q;
  logic both_q;

  always_ff @(posedge src_clk) begin
    a_src_q <= a_i;
    b_src_q <= b_i;
  end

  always_ff @(posedge dst_clk) begin
    a_s1_q <= a_src_q;
    a_s2_q <= a_s1_q;
    b_s1_q <= b_src_q;
    b_s2_q <= b_s1_q;
  end

  always_ff @(posedge dst_clk) both_q <= a_s2_q & b_s2_q;

  assign both_o = both_q;
endmodule

// Clock-gated consumer whose gate enable is registered in the producer domain.
// Library cell: body not supplied.
(* black_box *) module ckgate_hard (input logic clk_i, input logic en_i, output logic clk_o);
endmodule

module top (
  input  logic clk_a,
  input  logic rst_a_n,
  input  logic clk_src,
  input  logic rst_b_ni,
  input  logic en_i,
  output logic cnt_o
);
  logic       en_a_q, clk_b;
  logic [3:0] cnt_q;

  always_ff @(posedge clk_a or negedge rst_a_n)
    if (!rst_a_n) en_a_q <= 1'b0;
    else          en_a_q <= en_i;

  ckgate_hard u_cg (
    .clk_i (clk_src),
    .en_i  (en_a_q),
    .clk_o (clk_b)
  );

  always_ff @(posedge clk_b or negedge rst_b_ni)
    if (!rst_b_ni) cnt_q <= '0;
    else           cnt_q <= cnt_q + 4'd1;

  assign cnt_o = cnt_q[3];

endmodule

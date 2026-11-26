// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Unconditional zero-delay always_comb constant/copy feeding stage 1 of a 2-stage reset synchroniser is recognised.
// expect: RDC-003 INFO
// expect-none: RDC-003 WARNING
// expect-none: RDC-006
module top(input logic clka, rst_n, d, output logic q);
  logic s1, s2, d_o;
  always_comb d_o = 1'b1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) s1 <= 1'b0; else s1 <= d_o;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) s2 <= 1'b0; else s2 <= s1;
  always_ff @(posedge clka or negedge s2) if (!s2) q <= 1'b0; else q <= d;
endmodule

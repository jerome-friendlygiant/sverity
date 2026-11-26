// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Conditional always_comb driving stage 1's D is not a constant, so the chain is not a recognised reset synchroniser.
// expect: RDC-003 WARNING
// expect-none: RDC-003 INFO
module top(input logic clka, rst_n, en, d, output logic q);
  logic s1, s2, d_o;
  always_comb begin
    d_o = 1'b1;
    if (en) d_o = 1'b0;
  end
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) s1 <= 1'b0; else s1 <= d_o;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) s2 <= 1'b0; else s2 <= s1;
  always_ff @(posedge clka or negedge s2) if (!s2) q <= 1'b0; else q <= d;
endmodule

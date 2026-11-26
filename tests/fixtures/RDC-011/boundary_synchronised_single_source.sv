// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Two registers use asynchronous resets, but one is the output of a two-stage synchroniser of rst_a_n, so both trace to one
// assertion source: one source is not multi-source sequencing, so RDC-011 does not fire.
// expect-none: RDC-011
module top(input logic clka, clkb, rst_a_n, d, output logic q, r);
  logic s1, s2;
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) s1 <= 1'b0; else s1 <= 1'b1;
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) s2 <= 1'b0; else s2 <= s1;
  always_ff @(posedge clka or negedge s2) if (!s2) q <= 1'b0; else q <= d;
  always_ff @(posedge clkb or negedge rst_a_n) if (!rst_a_n) r <= 1'b0; else r <= d;
endmodule

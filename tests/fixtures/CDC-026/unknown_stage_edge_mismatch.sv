// sverity-args: --top top --sdc @DIR@/unknown_relation.sdc
// Clock relationship is unknown, so the mismatched-stage result must remain unresolved.
// expect: CDC-026 WARNING
// expect-confidence: CDC-026 unresolved
module top(input logic clka, clkb, rst_n, d, output logic q);
  logic a_q, s1;
  always_ff @(posedge clka) a_q <= d;
  always_ff @(posedge clkb) s1 <= a_q;
  always_ff @(negedge clkb) q <= s1;
endmodule

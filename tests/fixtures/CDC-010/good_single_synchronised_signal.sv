// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// MUTATION baseline: one synchronised signal feeds the logic.
// expect-none: CDC-010
// expect-none: CDC-011
module top(input logic clka, clkb, rst_n, d1, en, output logic q);
  logic a1, s1a, s1b;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a1 <= 1'b0; else a1 <= d1;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s1b, s1a} <= '0; else {s1b, s1a} <= {s1a, a1};
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= s1b & en;
endmodule

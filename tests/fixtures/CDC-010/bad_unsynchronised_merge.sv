// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: q <= a1 ^ a2; => q <= a1;
// revert-expect-none: CDC-010 WARNING
// expect: CDC-010 WARNING
module top(input logic clka, clkb, rst_n, d1, d2, output logic q);
  logic a1, a2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {a1, a2} <= '0; else {a1, a2} <= {d1, d2};
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= a1 ^ a2;
endmodule

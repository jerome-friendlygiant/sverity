// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Two independently synchronised signals from one domain are combined in the destination.
// expect: CDC-010 WARNING
module top(input logic clka, clkb, rst_n, d1, d2, output logic q);
  logic a1, a2, s1a, s1b, s2a, s2b;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {a1, a2} <= '0; else {a1, a2} <= {d1, d2};
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s1b, s1a, s2b, s2a} <= '0; else {s1b, s1a, s2b, s2a} <= {s1a, a1, s2a, a2};
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= s1b & s2b;
endmodule

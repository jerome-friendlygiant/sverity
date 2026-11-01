// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: {s1, a1 & a2} => {s1, a1}
// revert-expect-none: CDC-008 WARNING
// Glitch-prone combinational source in front of the synchroniser.
// expect: CDC-008 WARNING
module top(input logic clka, clkb, rst_n, d, e, output logic q);
  logic a1, a2, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {a2, a1} <= 2'b00; else {a2, a1} <= {e, d};
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= 2'b00; else {s2, s1} <= {s1, a1 & a2};
  assign q = s2;
endmodule

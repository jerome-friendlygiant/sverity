// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: always_ff @(posedge clkb or negedge rst_n) if (!rst_n) b_q => always_ff @(posedge clka or negedge rst_n) if (!rst_n) b_q
// revert-expect-none: CDC-013 WARNING
// clka (10 ns) is not faster than clkb (7 ns)? use a pair where the source is faster: clkb -> clka.
// expect: CDC-013 WARNING
module top(input logic clka, clkb, rst_n, d, output logic q);
  logic b_q, s1, s2;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) b_q <= 1'b0; else b_q <= d;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {s2, s1} <= '0; else {s2, s1} <= {s1, b_q};
  assign q = s2;
endmodule

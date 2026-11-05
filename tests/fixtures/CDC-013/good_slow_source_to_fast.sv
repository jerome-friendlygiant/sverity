// sverity-args: --top top --sdc @DIR@/slow.sdc
// expect-none: CDC-013
// Near miss of bad_fast_to_slow.sv: the source register is now in the slow clka domain (30 ns) and the synchroniser in the fast clkb domain (7 ns), so the source is not faster than the destination. The args use slow.sdc because with async.sdc (10 ns and 7 ns) the source period stays below 1.5 x the destination period, and the rule fires for any single-bit crossing in that pair.
module top(input logic clka, clkb, rst_n, d, output logic q);
  logic a_q, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= '0; else {s2, s1} <= {s1, a_q};
  assign q = s2;
endmodule

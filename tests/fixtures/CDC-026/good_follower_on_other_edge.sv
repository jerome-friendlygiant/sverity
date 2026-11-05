// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Both required stages agree; the register after them is not part of the synchroniser, so its edge does not matter here.
// expect-none: CDC-026
module top(input logic clka, clkb, d, output logic y);
  logic a_q, s1, s2;
  always_ff @(posedge clka) a_q <= d;
  always_ff @(posedge clkb) begin s1 <= a_q; s2 <= s1; end
  always_ff @(negedge clkb) y <= s2;
endmodule

// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Waiving the enable does not accept the missing synchroniser: the finding stays an open ERROR.
// expect: CDC-028 ERROR
// expect-none: CST-022
module top(input logic clka, clkb, d, en, output logic q);
  logic s;
  always_ff @(posedge clka) s <= d;
  // sverity-waive CDC-028 owner="team" reason="enable timing checked"
  always_ff @(posedge clkb) if (en) q <= s;
endmodule

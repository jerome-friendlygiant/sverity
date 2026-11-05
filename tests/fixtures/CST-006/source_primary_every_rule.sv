// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Each rule the crossing stands for has its own waiver, so the one finding is waived.
// expect: CDC-028 ERROR
// expect-none: CST-022
module top(input logic clka, clkb, d, en, output logic q);
  logic s;
  always_ff @(posedge clka) s <= d;
  // sverity-waive CDC-028 owner="team" reason="enable timing checked"
  // sverity-waive CDC-003 owner="team" reason="metastability accepted by review"
  always_ff @(posedge clkb) if (en) q <= s;
endmodule

// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// CDC-003 is evidence of the primary CDC-028 finding
// expect: CDC-028 ERROR
// expect: CST-022 WARNING
module top(input logic clka, clkb, rst_n, d, gate, output logic q);
  logic launched;
  always_ff @(posedge clka) launched <= d;
  // sverity-waive CDC-003 owner="team" reason="procedure reviewed" signal="top.q"
  // Ordinary comments and blank lines do not consume the waiver.

  // sverity-waive CDC-028 owner="team" reason="protocol reviewed" signal="top.q"
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) q <= 0;
    // sverity-waive CDC-003 owner="team" reason="assignment is not diagnostic location"
    else if (gate) q <= launched;
endmodule

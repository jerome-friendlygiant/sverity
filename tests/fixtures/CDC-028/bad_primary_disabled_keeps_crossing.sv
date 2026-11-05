// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --disable-rule CDC-028
// Disabling the rule that would be primary must not hide the crossing: the next description becomes the finding.
// expect: CDC-003 ERROR
// expect-none: CDC-028
module top(input logic clka, clkb, d, en, output logic q);
  logic launch;
  always_ff @(posedge clka) launch <= d;
  always_ff @(posedge clkb) if (en) q <= launch;
endmodule

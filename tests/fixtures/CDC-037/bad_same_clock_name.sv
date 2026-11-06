// sverity-args: --top top --sdc @DIR@/same2.sdc
// expect: CST-003 WARNING
// expect: CST-004 WARNING
// expect: CDC-003 WARNING
// Regression from the independent audit: this hazard must never be classified as safe.
// The later same-name declaration overrides the earlier source. The earlier port
// is now unconstrained, so this is Unknown (CST-004), rather than same-clock CDC-037.
module top(input logic clka, clkb, d, output logic q);
  logic a_q;
  always_ff @(posedge clka) a_q <= d;
  always_ff @(posedge clkb) q <= a_q;
endmodule

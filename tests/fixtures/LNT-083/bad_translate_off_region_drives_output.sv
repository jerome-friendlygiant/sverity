// sverity-args: --top top
// Simulation runs the assignment inside the region, synthesis does not: `z` has no driver for synthesis.
// expect: LNT-083 WARNING 7
// expect: LNT-024 WARNING 5
module top(input logic a, b, output logic y, output logic z);
  assign y = a;
  // synopsys translate_off
  assign z = a ^ b;
  // synopsys translate_on
endmodule

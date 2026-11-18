// sverity-args: --top top
// The wire is read by synthesized logic and driven only inside the region: undriven for synthesis.
// expect: LNT-083 WARNING 7
// expect: LNT-008 WARNING 6
module top(input logic a, b, output logic y);
  wire t;
  // pragma translate_off
  assign t = a ^ b;
  // pragma translate_on
  assign y = t;
endmodule

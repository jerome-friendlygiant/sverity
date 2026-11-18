// sverity-args: --top top
// expect: LNT-083 WARNING 5
module top(input logic a, output logic y);
  assign y = a;
  // synthesis translate_on
endmodule

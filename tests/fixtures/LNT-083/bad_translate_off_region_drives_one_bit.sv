// sverity-args: --top top
// Bit 1 of `t` is driven only inside the region: undriven for synthesis, while bit 0 is driven.
// expect: LNT-083 WARNING 8
// expect: LNT-008 WARNING 6
module top(input logic a, b, output logic [1:0] y);
  wire [1:0] t;
  assign t[0] = a;
  // synopsys translate_off
  assign t[1] = b;
  // synopsys translate_on
  assign y = t;
endmodule

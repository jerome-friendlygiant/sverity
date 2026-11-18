// sverity-args: --top top
// slang lists no driver for the write after the `continue` at i == 1, but it runs at i == 2: the output is not "never driven".
// expect: LNT-024 WARNING 5
module top(input logic c,
           output logic y);
  always @(c) for (int i = 0; i < 4; i++) begin if (i == 1) continue; if (i == 2) y = c; end
endmodule

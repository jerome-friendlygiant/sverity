// sverity-args: --top top
// expect-none: LNT-035
module top(input logic [5:0] n, output logic [63:0] w, output logic [31:0] o);
  assign w = 1 << n;                // context-determined: the 1 is 64 bits
  assign o = {1 << n[4:0]};         // 32-bit concatenation: the shift stays 32 bits wide
endmodule

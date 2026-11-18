// sverity-args: --top top
// A 3-bit counter compared with 8 never reaches 8: it wraps, so the values it takes are not 0..7 as counted.
// expect: LNT-041 WARNING 5
module top(input logic c, output logic y);
  logic a [0:7];
  initial for (bit [2:0] i = 0; i < 8; i++) a[i] = c;
  assign y = a[0];
endmodule

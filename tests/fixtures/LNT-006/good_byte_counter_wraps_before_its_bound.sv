// sverity-args: --top top
// A byte counter compared with 8'd200 never leaves 0..127 in range: it wraps to -128 and the loop ends there. The loop is not
// counted as 0..199, so no index overrun is claimed.
// expect-none: LNT-006
module top(input logic c, output logic y);
  logic a [-128:127];
  always_comb for (byte i = 0; i < 8'd200; i++) a[i] = c;
  assign y = a[0];
endmodule

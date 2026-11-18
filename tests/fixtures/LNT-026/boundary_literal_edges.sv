// sverity-args: --top top
// Largest value that fits is clean; one more bit is not. A signed decimal spends one bit on the sign.
// expect: LNT-026 WARNING 8
// expect: LNT-026 WARNING 10
// expect-count: LNT-026 WARNING 2
module top(output logic [3:0] u_fit, u_over, output logic signed [3:0] s_fit, s_over);
  assign u_fit = 4'd15;             // 4 bits needed, 4 available
  assign u_over = 4'd16;            // 5 bits needed
  assign s_fit = 4'sd7;             // sign bit plus 3
  assign s_over = 4'sd8;            // sign bit plus 4
endmodule

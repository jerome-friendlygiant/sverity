// sverity-args: --top top
// A `?:` arm is converted to the type of the whole conditional. With an unsigned arm the conditional is unsigned, so the
// signed 2'sbzz is zero-extended, not sign-extended: the upper two bits of w are 0, not z. Simulators agree (iverilog gives
// 000000zz). The same arm with a signed conditional sign-extends (boundary_signed_z_sign_extended.sv) and stays INFO.
// expect: LNT-039 WARNING 7
module top(input logic s, output wire [7:0] w);
  assign w = s ? 2'sbzz : 2'b00;
endmodule

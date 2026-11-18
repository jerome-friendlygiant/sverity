// sverity-args: --top top
// 4'(9) is signed -7: the unsigned comparison sees 9, so the ordered test differs between the signednesses.
// revert: (n < 4'(9)) => (n < 4'(7))
// revert-expect-none: LNT-005 WARNING
// expect: LNT-005 WARNING 7
module top (input logic [3:0] n, output logic y);
  assign y = (n < 4'(9));
endmodule

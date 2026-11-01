// sverity-args: --top top
// 'x is assigned to sel and sel steers a later assignment: the branch taken differs between simulation (X) and hardware
// expect: LNT-066 WARNING
module top(input logic a, b, output logic y);
  logic sel;
  assign sel = 1'bx;
  always_comb if (sel) y = a; else y = b;
endmodule

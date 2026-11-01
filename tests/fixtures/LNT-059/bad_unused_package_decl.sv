// sverity-args: --top top
// Unused package declarations are reported through slang (unused-package-*).
// expect: LNT-059 WARNING
package p;
  localparam int UNUSED_P = 3;
  localparam int USED_P = 4;
endpackage
module top(output int y);
  assign y = p::USED_P;
endmodule

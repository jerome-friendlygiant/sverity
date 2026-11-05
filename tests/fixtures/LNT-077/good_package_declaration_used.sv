// sverity-args: --top top
// expect-none: LNT-077
package p; localparam int USED_P = 4; endpackage
module top(output int y);
  assign y = p::USED_P;
endmodule

// sverity-args: --top top
// expect-none: LNT-059
package pkg; localparam int W = 4; endpackage
module top(output int y);
  import pkg::*;
  assign y = W;
endmodule

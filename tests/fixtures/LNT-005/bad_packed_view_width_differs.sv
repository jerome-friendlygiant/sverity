// sverity-args: --top top
// 16 bits into a 32-bit packed view: the extension is a width change, not a view of the same bits.
// revert: input logic [15:0] w => input logic [31:0] w
// revert-expect-none: LNT-005 WARNING
// expect: LNT-005 WARNING 7
module top(input logic [15:0] w, output logic [3:0][7:0] bytes);
  assign bytes = w;
endmodule

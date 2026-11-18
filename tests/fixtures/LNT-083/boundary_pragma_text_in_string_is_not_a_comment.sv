// sverity-args: --top top
// The words of a pragma inside a string literal are text, not a comment.
// expect-none: LNT-083
module top(input logic a, output logic y);
  localparam string S = "// synopsys translate_off";
  assign y = a;
endmodule

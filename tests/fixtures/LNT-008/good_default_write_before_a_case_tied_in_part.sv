// sverity-args: --top top
// The selector is tied in part, which the analysis does not decide, but r has a default write before the case: every instance drives it.
// expect-none: LNT-008
module child(input logic [1:0] en, input logic [7:0] a, b, output logic [7:0] q);
  logic [7:0] r;
  always_comb begin
    r = a;
    case (en) 2'b10: r = b; default: ; endcase
  end
  assign q = r;
endmodule
module top(input logic x, input logic [7:0] a, b, output logic [7:0] q);
  child u(.en({1'b0, x}), .a, .b, .q);
endmodule

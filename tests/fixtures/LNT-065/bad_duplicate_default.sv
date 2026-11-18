// sverity-args: --top top
// revert: default: y = 1'b0; default: y = 1'b1; => default: y = 1'b0;
// revert-expect-none: LNT-065
// Two default labels in one case (slang MultipleDefaultCases, mapped to LNT-065).
// expect: LNT-065 ERROR
module top(input logic [1:0] s, output logic y);
  always_comb begin
    case (s)
      2'd0: y = 1'b1;
      2'd1: y = 1'b0;
      default: y = 1'b0; default: y = 1'b1;
    endcase
  end
endmodule

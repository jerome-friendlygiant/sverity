// sverity-args: --top top
// A casex written by a macro is reported at each use: two uses are two findings, at the two call lines (not the define line).
// expect: LNT-016 WARNING 11
// expect: LNT-016 WARNING 17
// expect-count: LNT-016 WARNING 2
// revert: casex (e) => casez (e)
// revert-expect-none: LNT-016
`define CX(e) casex (e)
module top(input logic [1:0] s, t, output logic y, z);
  always_comb begin
    `CX(s)
      2'b1x: y = 1'b1;
      default: y = 1'b0;
    endcase
  end
  always_comb begin
    `CX(t)
      2'b1x: z = 1'b1;
      default: z = 1'b0;
    endcase
  end
endmodule

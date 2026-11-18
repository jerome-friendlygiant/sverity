// sverity-args: --top top
// `casex ... matches` is a casex statement: X and Z in its expression are wildcards, so it is reported at the keyword like plain casex.
// expect: LNT-016 WARNING 10
// expect-count: LNT-016 WARNING 1
// revert: casex (s) matches => casez (s) matches
// revert-expect-none: LNT-016
module top(input logic [1:0] s, output logic y);
  always_comb begin
    y = 1'b0;
    casex (s) matches
      2'b1?: y = 1'b1;
      default: y = 1'b0;
    endcase
  end
endmodule

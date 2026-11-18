// sverity-args: --top top
// Near-miss of bad_case_without_default: a default label is present, placed before the other items.
// expect-none: LNT-065
module top(input logic [1:0] s, output logic y);
  always_comb begin
    case (s)
      default: y = 1'b1;
      2'd1: y = 1'b0;
    endcase
  end
endmodule

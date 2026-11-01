// sverity-args: --top top
// Default assignment first, case with default: every path assigns.
// expect-none: LNT-013
module top(input logic [1:0] s, input logic d, output logic y, z);
  always @* begin
    y = 1'b0;
    if (s == 2'd1) y = d;
    case (s)
      2'd0: z = d;
      default: z = 1'b0;
    endcase
  end
endmodule

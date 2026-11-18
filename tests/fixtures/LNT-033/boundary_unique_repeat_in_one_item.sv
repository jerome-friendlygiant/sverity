// sverity-args: --top top
// expect-none: LNT-033
// A value listed twice in one item selects one branch: no two items overlap.
module top(input logic [1:0] s, output logic [1:0] y);
  always_comb begin
    y = 2'd0;
    unique case (s)
      2'd0, 2'd0: y = 2'd1;
      2'd1: y = 2'd2;
      default: y = 2'd3;
    endcase
  end
endmodule

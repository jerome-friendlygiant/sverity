// sverity-args: --top top
// expect-none: LNT-033
// A signed selector and signed bounds compare signed: [-2:1] and [2:3] are disjoint.
module top(input logic signed [3:0] s, output logic [1:0] y);
  always_comb begin
    y = 2'd0;
    unique case (s) inside
      [-2:1]: y = 2'd1;
      [2:3]: y = 2'd2;
      default: y = 2'd3;
    endcase
  end
endmodule

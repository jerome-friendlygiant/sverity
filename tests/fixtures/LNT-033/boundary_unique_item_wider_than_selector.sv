// sverity-args: --top top
// expect-none: LNT-033
// Items wider than the selector are compared at the full width (IEEE 1800 12.5): 3'b100 never matches a 2-bit selector.
module top(input logic [1:0] s, output logic [1:0] y);
  always_comb begin
    y = 2'd0;
    unique case (s)
      3'b000: y = 2'd1;
      3'b100: y = 2'd2;
      default: y = 2'd3;
    endcase
  end
endmodule

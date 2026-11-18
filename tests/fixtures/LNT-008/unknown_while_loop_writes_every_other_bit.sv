// sverity-args: --top top
// A while loop is not unrolled by the analysis: the bits it reaches are not known, so none is claimed driven.
// expect: LNT-008 WARNING 5
module top(output logic [7:0] y);
  logic [7:0] v;
  always_comb begin
    int i;
    i = 0;
    while (i < 8) begin
      v[i] = 1'b1;
      i = i + 2;
    end
  end
  assign y = v;
endmodule

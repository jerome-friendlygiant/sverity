// sverity-args: --top top
// expect: LNT-033 WARNING 6
// expect-none: LNT-081
module top(input logic [1:0] s, input logic a, output logic [1:0] y);
  always_comb begin
    unique if (s == 2'd0 && a) y = 2'd1;
    else if (s == 2'd0) y = 2'd2;
    else y = 2'd3;
  end
endmodule

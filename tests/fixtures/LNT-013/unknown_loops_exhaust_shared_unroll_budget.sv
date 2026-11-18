// sverity-args: --top top
// Each loop is under the unroll budget, but together they are over it: the second is not walked, so x2 cannot be called unassigned.
// expect: LNT-013 WARNING 6
module top(input logic [3:0] a, output logic [3:0] y1, y2);
  logic [3:0] x1, x2;
  always @* begin
    for (int i = 0; i < 6000; i++) x1 = a;
    for (int i = 0; i < 6000; i++) if (i == 5999) x2 = a;
  end
  assign y1 = x1; assign y2 = x2;
endmodule

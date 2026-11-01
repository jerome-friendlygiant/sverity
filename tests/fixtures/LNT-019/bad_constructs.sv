// sverity-args: --top top
// expect: LNT-019 WARNING 10
// expect: LNT-019 WARNING 11
// (the initial block on line 13 is an initialisation assumption: LNT-048)
// expect: LNT-019 WARNING 15
// expect: LNT-019 WARNING 16
// expect: LNT-019 WARNING 17
// expect: LNT-019 WARNING 18
module top(input logic clk, input logic [7:0] n, output logic q);
  real r;
  string s;
  event ev;
  initial q = 0;
  always @(posedge clk) begin
    #1 q <= n[0];
    wait (n[0]);
    ->ev;
    forever q <= ~q;
  end
endmodule

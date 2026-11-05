// sverity-args: --top top
// (the initial block on line 13 is an initialisation assumption: LNT-048)
// expect: LNT-019 WARNING 11
// expect: LNT-019 WARNING 12
// expect: LNT-019 WARNING 13
// expect: LNT-019 WARNING 14
module top(input logic clk, input logic [7:0] n, output logic q);
  event ev;
  initial q = 0;
  always @(posedge clk) begin
    #1 q <= n[0];
    wait (n[0]);
    ->ev;
    forever q <= ~q;
  end
endmodule

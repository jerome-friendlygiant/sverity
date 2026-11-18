// sverity-args: --top top
// Both procedures use `i` as a loop counter, but each also assigns it after the loop: the value is state, so the two
// procedures are two drivers. slang keeps one driver per procedure, so only the code shows the extra write.
// expect: LNT-009 ERROR 8
module top(input logic clk, output int y);
  int i;
  always @(posedge clk) begin for (i = 0; i < 4; i++) ; i = 9; end
  always @(posedge clk) begin for (i = 0; i < 2; i++) ; i = 3; end
  assign y = i;
endmodule

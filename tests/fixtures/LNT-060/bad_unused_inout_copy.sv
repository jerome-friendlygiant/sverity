// sverity-args: --top top
// expect: LNT-060 WARNING
module top(input logic clk);
  logic q;
  task automatic get(inout logic v); logic t; t = 1; endtask
  always @(posedge clk) q = 1;
  always @* get(q);
endmodule
